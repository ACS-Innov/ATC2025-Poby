#include "dpu/decompress_client_epoll.h"
#include "compress.pb.h"
#include "doca/common.h"
#include "doca/doca_buf.h"
#include "doca_error.h"
#include "dpu/metadata.h"
#include "utils/blob_pool.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <ratio>
#include <spdlog/spdlog.h>
#include <utils/MsgFrame.h>
namespace hdc {
namespace dpu {
DecompressClientEpoll::DecompressClientEpoll(
    CompressEngine compress_engine,
    // DmaEngine dma_engine,
    EventLoop *loop, const InetAddress &listen_addr, RdmaConfig rdma_config,
    std::shared_ptr<BlobPool> blob_pool)
    : compress_engine_(std::move(compress_engine)),
      // dma_engine_(std::move(dma_engine)),
      client_(loop, listen_addr, "DecompressClientEpoll",
              std::move(rdma_config)),
      loop_(loop), blob_pool_(std::move(blob_pool)) {
  client_.setConnectedCallback(
      [this](const RdmaConnectionPtr &conn) { this->onConnected(conn); });
  client_.setRecvSuccessCallback([this](const RdmaConnectionPtr &conn,
                                        uint8_t *recv_buf, uint32_t recv_len,
                                        const ibv_wc &wc) {
    this->onRecvSuccess(conn, recv_buf, recv_len, wc);
  });
  client_.setRecvFailCallback(
      [this](const RdmaConnectionPtr &conn, const ibv_wc &wc) {
        this->onRecvFail(conn, wc);
      });
  client_.setSendCompleteSuccessCallback(
      [this](const RdmaConnectionPtr &conn, const ibv_wc &wc) {
        this->onSendCompleteSuccess(conn, wc);
      });
  client_.setSendCompleteFailCallback(
      [this](const RdmaConnectionPtr &conn, const ibv_wc &wc) {
        this->onSendCompleteFail(conn, wc);
      });

  compress_engine_.setCompressSuccessCallback(
      [this](CompressEngine &engine, uint64_t job_id, uint8_t *src_addr,
             size_t src_len, uint8_t *dst_addr, size_t dst_len) {
        this->onDecompressTaskSuccess(engine, job_id, src_addr, src_len,
                                      dst_addr, dst_len);
      });
  compress_engine_.setCompressErrorCallback(
      [this](CompressEngine &engine, doca_error_t err) {
        this->onDecompressTaskError(engine, err);
      });
}

void DecompressClientEpoll::connect() {
  loop_->runInLoop([this]() { client_.connect(); });
}

void DecompressClientEpoll::decompressStart() {
  compress_engine_.start();
}

bool DecompressClientEpoll::handleDecompressFinishResponse(
    const compress::DecompressFinishResponse &resp) {
  SPDLOG_INFO("Recv DecompressFinishResponse: layer {}, idx {}, success {}, "
              "data_inline {}, bufpair_id {}",
              resp.layer_name(), resp.segment_idx(), resp.success(),
              resp.data_inline(), resp.bufpair_id());
  if (!pending_rdma_jobs_.empty()) {
    auto free_buf = conn_->acquireFreeSendBuf();
    auto info = std::move(pending_rdma_jobs_.front());
    pending_rdma_jobs_.pop_front();
    sendDecompressFinishRequest(std::move(info), *free_buf);
  }
  if (!resp.data_inline()) {
    compress_engine_.releaseFreeBufpair(resp.bufpair_id());
    if (!decompress_busy_) {
      if (!tryStartDecompressJob()) {
        SPDLOG_ERROR("tryStartDecompressJob error");
        return false;
      }
    }
  } else {
    dma_busy_ = false;
  }
  return true;
}

bool DecompressClientEpoll::startDecompressJob(ContentElement &task,
                                               size_t bufpair_id) {
  // record decompress time
  start_time_ = std::chrono::high_resolution_clock::now();

  compress_job_.image_name_tag = std::move(task.image_name_tag);
  compress_job_.layer = std::move(task.layer);
  compress_job_.segment_idx = task.segment_idx;
  compress_job_.total_segments = task.total_segments;
  compress_job_.bufpair_id = bufpair_id;

  auto &segment = task.segment;
  auto &bufpair = compress_engine_.get_bufpair(bufpair_id);
  assert(bufpair.src_mem.size() >= segment.get_size());
  memcpy(bufpair.src_mem.data(), segment.get_addr(), segment.get_size());
  if (bufpair.src_doca_buf.set_data_by_offset(0, segment.get_size()) !=
      DOCA_SUCCESS) {
    SPDLOG_ERROR("set doca buf error");
    return false;
  }
  blob_pool_->releaseBlob(std::move(segment));

  compress_engine_.start_job(job_id_++, DOCA_DECOMPRESS_DEFLATE_JOB,
                             bufpair_id);
  decompress_busy_ = true;
  SPDLOG_INFO("enqueue DOCA Decompress job {}. image {}, layer {}, idx {}-{}, "
              "bufpair_id {}",
              job_id_ - 1, compress_job_.image_name_tag, compress_job_.layer,
              compress_job_.segment_idx, compress_job_.total_segments,
              bufpair_id);
  return true;
}

bool DecompressClientEpoll::tryStartDecompressJob() {
  assert(connected_ && !decompress_busy_);
  if (pending_compress_jobs_.empty()) {
    return true;
  }
  auto bufpair_id = compress_engine_.acquireFreeBufpair();
  if (!bufpair_id.has_value()) {
    return true;
  }
  auto &task = pending_compress_jobs_.front();
  if (!startDecompressJob(task, *bufpair_id)) {
    SPDLOG_ERROR("startDecompressJob error");
    return false;
  }
  pending_compress_jobs_.pop_front();
  return true;
}


void DecompressClientEpoll::onConnected(const RdmaConnectionPtr &conn) {

  conn_ = conn;
  SPDLOG_INFO("DecompressClientEpoll connected");
  // Send MmapInfoRequest
  auto free_buf = conn->acquireFreeSendBuf();
  inflight_sends_.emplace_back(free_buf->id);
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;
  auto mmap_info_req = compress::MmapInfoRequest{};
  mmap_info_req.set_mmap_num(compress_engine_.bufpair_num());
  *reinterpret_cast<int *>(send_buf) = static_cast<int>(MsgType::kMmapInfo);
  auto frame_len = serializeRdmaPbMsg(
      send_buf + sizeof(MsgType), send_cap - sizeof(MsgType), mmap_info_req);
  if (frame_len == -1) {
    SPDLOG_ERROR("serialize MmapInfoRequest error");
    return;
  }
  SPDLOG_INFO("Send MmapInfoRequest");
  conn->send(send_buf, frame_len + sizeof(MsgType), wr_id_++);
  return;
}

void DecompressClientEpoll::onRecvSuccess(const RdmaConnectionPtr &conn,
                                          uint8_t *recv_buf, uint32_t recv_len,
                                          const ibv_wc &wc) {
  if (recv_len < sizeof(int)) {
    SPDLOG_ERROR("recv_len < 4");
    return;
  }
  conn_->releaseSendBuf(inflight_sends_.front());
  inflight_sends_.pop_front();

  int type = *reinterpret_cast<int *>(recv_buf);
  if (type == static_cast<int>(MsgType::kMmapInfo)) {
    compress::MmapInfoResponse resp{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        resp)) {
      SPDLOG_ERROR("parse DecompressConnectionResponse error");
      return;
    }
    if (!handleMmapInfoResponse(resp)) {
      SPDLOG_ERROR("handle DecompressConnectionResponse error");
      return;
    }
  } else if (type == static_cast<int>(MsgType::kDecompressFinish)) {
    compress::DecompressFinishResponse resp{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        resp)) {
      SPDLOG_ERROR("parse OffloadResponse error");
      return;
    }
    if (!handleDecompressFinishResponse(resp)) {
      SPDLOG_ERROR("handlre OffloadResponse error");
      return;
    }
  } else {
    SPDLOG_ERROR("Request type error");
  }
}

void DecompressClientEpoll::onRecvFail(const RdmaConnectionPtr &conn,
                                       const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA recv complete fail. wr_id: {}", wc.wr_id);
}

void DecompressClientEpoll::onSendCompleteSuccess(const RdmaConnectionPtr &conn,
                                                  const ibv_wc &wc) {}

void DecompressClientEpoll::onSendCompleteFail(const RdmaConnectionPtr &conn,
                                               const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA send complete fail. wr_id: {}", wc.wr_id);
}

void DecompressClientEpoll::onDecompressTaskSuccess(
    CompressEngine &engine, uint64_t job_id, uint8_t *src_addr, size_t src_len,
    uint8_t *dst_addr, size_t dst_len) {

  RdmaInfo info{compress_job_.image_name_tag,
                compress_job_.layer,
                compress_job_.total_segments,
                compress_job_.segment_idx,
                dst_len,
                compress_job_.bufpair_id,
                false};

  auto free_buf = conn_->acquireFreeSendBuf();

  if (free_buf.has_value()) {
    sendDecompressFinishRequest(std::move(info), *free_buf);
  } else {
    pending_rdma_jobs_.push_back(std::move(info));
  }
  decompress_busy_ = false;

  // decompress time
  auto now = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration<double, std::milli>(now - start_time_).count();
  if (compress_job_.image_name_tag != curr_image_tag_) {
    decompress_duration_ = 0;
    curr_image_tag_ = compress_job_.image_name_tag;
  }
  decompress_duration_ += duration;

  SPDLOG_INFO("Decompress job {} success, image {}, decompress_rtt {}ms, "
              "decompress_duration {}ms",
              job_id, compress_job_.image_name_tag, duration,
              decompress_duration_);

  if (!tryStartDecompressJob()) {
    SPDLOG_ERROR("tryStartDecompressJob error");
  }
}

void DecompressClientEpoll::onDecompressTaskError(CompressEngine &engine,
                                                  doca_error_t err) {
  SPDLOG_ERROR("Decompress Task error: {}", doca_get_error_string(err));
}


bool DecompressClientEpoll::handleMmapInfoResponse(
    const compress::MmapInfoResponse &resp) {
  SPDLOG_INFO("recv MmapInfoResponse");
  std::vector<ExportDescRemote> export_descs;
  export_descs.reserve(resp.mmaps_size());
  for (int i = 0, n = resp.mmaps_size(); i < n; ++i) {
    auto &export_mmap = resp.mmaps(i);
    export_descs.emplace_back(
        export_mmap.export_desc().c_str(), export_mmap.export_desc().size(),
        reinterpret_cast<uint8_t *>(export_mmap.addr()), export_mmap.len());
  }

  auto res = compress_engine_.src_mmaps_start(std::nullopt);
  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("src buf mmap error: {}", doca_get_error_string(res));
    return false;
  }

  res = compress_engine_.dst_mmaps_create_from_export(export_descs);

  if (res != DOCA_SUCCESS) {
    SPDLOG_ERROR("dst buf mmap error: {}", doca_get_error_string(res));
    return false;
  }

  connected_ = true;
  if (!tryStartDecompressJob()) {
    SPDLOG_ERROR("tryStartDecompressJob error");
    return false;
  }

  return true;
}

void DecompressClientEpoll::submitDecompressTask(ContentElement content) {
  loop_->runInLoop([this, cont = std::move(content)]() {
    if (cont.is_compressed) {
      pending_compress_jobs_.emplace_back(std::move(cont));
      if (connected_ && !decompress_busy_) {
        tryStartDecompressJob();
      }
    } else {
      // pending_dma_jobs_.emplace_back(std::move(cont));
      // if (connected_ && !dma_busy_) {
      //   tryStartDmaJob();
      // }

      size_t segment_size = cont.segment.get_size();
      RdmaInfo info{std::move(cont.image_name_tag),
                    std::move(cont.layer),
                    cont.total_segments,
                    cont.segment_idx,
                    segment_size,
                    0,
                    true,
                    std::move(cont.segment)};
      auto free_buf = conn_->acquireFreeSendBuf();
      if (!free_buf.has_value()) {
        pending_rdma_jobs_.push_back(std::move(info));
      } else {
        sendDecompressFinishRequest(std::move(info), *free_buf);
      }
    }
  });
}

void DecompressClientEpoll::sendDecompressFinishRequest(
    RdmaInfo info, const RdmaConnection::SendBuf &free_buf) {
  compress::DecompressFinishRequest req{};
  req.set_image_name_tag(std::move(info.image_name_tag));
  req.set_layer_name(std::move(info.layer));
  req.set_segment_idx(info.segment_idx);
  req.set_segment_size(info.dst_len);
  req.set_bufpair_id(info.mmap_id);
  req.set_total_segments(info.total_segments);
  req.set_data_inline(info.data_inline);

  inflight_sends_.emplace_back(free_buf.id);
  auto send_buf = free_buf.addr;
  auto send_cap = free_buf.cap;
  *reinterpret_cast<int *>(send_buf) =
      static_cast<int>(MsgType::kDecompressFinish);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), req);
  if (frame_len == -1) {
    SPDLOG_ERROR("serialize DecompressFinishRequest error");
    return;
  }
  if (req.data_inline()) { 
    memcpy(send_buf + frame_len + sizeof(MsgType), info.segment.get_addr(),
           req.segment_size());
    conn_->send(send_buf, frame_len + sizeof(MsgType) + req.segment_size(),
                wr_id_++);
    blob_pool_->releaseBlob(std::move(info.segment));

  } else {
    conn_->send(send_buf, frame_len + sizeof(MsgType), wr_id_++);
  }
  SPDLOG_INFO("Send DecompressFinishRequest. image: {}, layer: {}, idx: {}, "
              "total_segments: {}, size: {}, bufpair_id: {}",
              req.image_name_tag(), req.layer_name(), req.segment_idx(),
              req.total_segments(), req.segment_size(), req.bufpair_id());
}
} // namespace dpu
} // namespace hdc