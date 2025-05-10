
#include "compress.pb.h"
#include "doca/common.h"
#include "doca_error.h"
#include "network/rdma/RdmaServer.h"
#include <cassert>
#include <cstdint>
#include <host/client/decompress_server_epoll.h>
#include <network/rdma/RdmaConnection.h>
#include <spdlog/spdlog.h>
#include <utils/MsgFrame.h>
namespace hdc::host::client {

DecompressServerEpoll::DecompressServerEpoll(
    CompressEngine compress_engine,
    EventLoop *loop, const InetAddress &listen_addr, RdmaConfig rdma_config,
    size_t untar_num_threads, std::string untar_file_path,
    OffloadClientEpoll *offload_client) noexcept
    : compress_engine_(std::move(compress_engine)),
      server_(loop, listen_addr, "DecompressServer", std::move(rdma_config)),
      untar_engine_(untar_num_threads, offload_client,
                    std::move(untar_file_path)) {
  server_.setConnectedCallback(
      [this](const RdmaConnectionPtr &conn) { this->onConnected(conn); });
  server_.setRecvSuccessCallback([this](const RdmaConnectionPtr &conn,
                                        uint8_t *recv_buf, uint32_t recv_len,
                                        const ibv_wc &wc) {
    this->onRecvSuccess(conn, recv_buf, recv_len, wc);
  });
  server_.setRecvFailCallback(
      [this](const RdmaConnectionPtr &conn, const ibv_wc &wc) {
        this->onRecvFail(conn, wc);
      });
  server_.setSendCompleteSuccessCallback(
      [this](const RdmaConnectionPtr &conn, const ibv_wc &wc) {
        this->onSendCompleteSuccess(conn, wc);
      });
  server_.setSendCompleteFailCallback(
      [this](const RdmaConnectionPtr &conn, const ibv_wc &wc) {
        this->onSendCompleteFail(conn, wc);
      });
}

void DecompressServerEpoll::start() { server_.start(); }

void DecompressServerEpoll::onConnected(const RdmaConnectionPtr &conn) {
  SPDLOG_INFO("DecompressServer connected");
}

void DecompressServerEpoll::onRecvSuccess(const RdmaConnectionPtr &conn,
                                          uint8_t *recv_buf, uint32_t recv_len,
                                          const ibv_wc &wc) {
  if (recv_len < sizeof(int)) {
    SPDLOG_ERROR("recv_len < 4");
    return;
  }
  int type = *reinterpret_cast<int *>(recv_buf);
  if (type == static_cast<int>(MsgType::kMmapInfo)) {
    compress::MmapInfoRequest req{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        req)) {
      SPDLOG_ERROR("parse MmapInfoRequest error");
      return;
    }
    if (!handleMmapInfoRequest(conn, req)) {
      SPDLOG_ERROR("handle MmapInfoRequest error");
      return;
    }
  } else if (type == static_cast<int>(MsgType::kDecompressFinish)) {
    compress::DecompressFinishRequest req{};
    auto frame_len = parseRdmaPbMsg(recv_buf + sizeof(MsgType),
                                    recv_len - sizeof(MsgType), req);
    if (frame_len == -1) {
      SPDLOG_ERROR("parse DecompressFinishRequest error");
      return;
    }
    if (!handleDecompressFinishRequest(
            conn, req, recv_buf + sizeof(MsgType) + frame_len,
            recv_len - frame_len - sizeof(MsgType))) {
      SPDLOG_ERROR("handle DecompressFinishRequest error");
      return;
    }
  } else {
    SPDLOG_ERROR("Request type error");
  }
}

bool DecompressServerEpoll::handleMmapInfoRequest(
    const RdmaConnectionPtr &conn, const compress::MmapInfoRequest &req) {
  SPDLOG_INFO("recv MmapInfoRequest");
  assert(compress_engine_.get_bufpair(0).dst_mem.size() == MAX_FILE_SIZE);
  assert(req.mmap_num() == compress_engine_.bufpair_num());

  // compress engine
  auto res = compress_engine_.dst_mmaps_start(
      std::make_optional(DOCA_ACCESS_DPU_READ_WRITE));
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("dst buf mmap start error: {}", doca_get_error_string(res));
    return false;
  }
  auto export_descs = compress_engine_.dst_mmaps_export_dpu();
  if (!export_descs.has_value()) {
    SPDLOG_ERROR("export dst mmap to DPU error: {}",
                 doca_get_error_string(export_descs.error()));
  }
  auto resp = compress::MmapInfoResponse();
  for (int i = 0, n = export_descs->size(); i < n; ++i) {
    auto mmap_element = resp.add_mmaps();
    auto &desc = (*export_descs)[i];
    auto &bufpair = compress_engine_.get_bufpair(i);
    mmap_element->set_addr(reinterpret_cast<uint64_t>(bufpair.dst_mem.data()));
    mmap_element->set_len(bufpair.dst_mem.size());
    mmap_element->set_export_desc(
        std::string(reinterpret_cast<const char *>(desc.export_desc),
                    desc.export_desc_len));
  }

  auto free_buf = conn->acquireFreeSendBuf();
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;
  *reinterpret_cast<int *>(send_buf) = static_cast<int>(MsgType::kMmapInfo);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), resp);
  if (frame_len == -1) {
    SPDLOG_ERROR("serialize MmapInfoResponse error");
    return false;
  }
  SPDLOG_INFO("Send MmapInfoResponse");
  conn->send(send_buf, sizeof(MsgType) + frame_len, wr_id_++);
  conn->releaseSendBuf(free_buf->id);
  return true;
}

bool DecompressServerEpoll::handleDecompressFinishRequest(
    const RdmaConnectionPtr &conn, const compress::DecompressFinishRequest &req,
    uint8_t *remain_buf, int remain_len) {

  SPDLOG_INFO("recv DecompressFinishRequest: image {}, layer {} seg {}-{}, "
              "seg_size {}, bufpair_id {}, data_inline {}",
              req.image_name_tag(), req.layer_name(), req.segment_idx(),
              req.total_segments(), req.segment_size(), req.bufpair_id(),
              req.data_inline());
  std::vector<uint8_t> data = [&req, this, remain_buf, remain_len]() {
    if (req.data_inline()) {
      return std::vector<uint8_t>(remain_buf, remain_buf + remain_len);

    } else {

      auto &dst_mem = compress_engine_.get_bufpair(req.bufpair_id()).dst_mem;
      assert(req.segment_size() <= dst_mem.size());
      return std::vector<uint8_t>(dst_mem.begin(),
                                  dst_mem.begin() + req.segment_size());
    }
  }();

  // send response
  compress::DecompressFinishResponse resp{};
  resp.set_success(true);
  resp.set_layer_name(req.layer_name());
  resp.set_segment_idx(req.segment_idx());
  resp.set_data_inline(req.data_inline());
  resp.set_bufpair_id(req.bufpair_id());

  auto free_buf = conn->acquireFreeSendBuf();
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;
  *reinterpret_cast<int *>(send_buf) =
      static_cast<int>(MsgType::kDecompressFinish);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), resp);
  if (frame_len == -1) {
    SPDLOG_ERROR("serialize DecompressFinishResponse error");
    return false;
  }
  SPDLOG_INFO("Send DecompressFinishResponse: layer {}, idx {}, success {}, "
              "bufpair_id {}. wr_id {}",
              resp.layer_name(), resp.segment_idx(), resp.success(),
              resp.bufpair_id(), wr_id_);

  conn->send(send_buf, sizeof(MsgType) + frame_len, wr_id_++);
  conn->releaseSendBuf(free_buf->id);
  untar_engine_.untar(UntarData{req.layer_name(), req.image_name_tag(),
                                std::move(data), req.segment_idx(),
                                req.total_segments()});
  return true;
}

void DecompressServerEpoll::onRecvFail(const RdmaConnectionPtr &conn,
                                       const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA recv complete fail. wr_id: {}", wc.wr_id);
}

void DecompressServerEpoll::onSendCompleteSuccess(const RdmaConnectionPtr &conn,
                                                  const ibv_wc &wc) {}

void DecompressServerEpoll::onSendCompleteFail(const RdmaConnectionPtr &conn,
                                               const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA send complete fail. wr_id: {}", wc.wr_id);
}
} // namespace hdc::host::client