#include <dpu/offload_server_epoll.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

DEFINE_string(content_client_ib_dev_name, "mlx5_2",
              "The IB device name of host ContentClient");
DEFINE_int32(content_client_ib_dev_port, 1,
             "The IB device port of host ContentClient");
DEFINE_string(content_client_peer_ip, "172.24.46.186",
              "The IP address of host ContentClient to connect");
DEFINE_uint32(content_client_peer_port, 9002,
              "The IP port of host ContentClient to connect");
DEFINE_uint64(
    content_client_rdma_mem, 129 * 1024 * 1024,
    "The memory of RDMA sendbuf/recvbuf for host ContentClient in bytes");
DEFINE_uint64(content_client_rdma_mem_num, 4,
              "The number of RDMA buf for host ContentClient");
DEFINE_string(content_leveldb_dir, "/tmp/testdb", "The root dir for leveldb");
namespace hdc {
namespace dpu {

OffloadServerEpoll::OffloadServerEpoll(
    EventLoop *loop, const InetAddress &listen_addr, RdmaConfig rdma_config,
    ContentFetcherPtr fetcher,
    DecompressClientEpoll *decompress_client) noexcept
    : server_(loop, listen_addr, "OffloadServerEpoll", std::move(rdma_config)),
      fetcher_(std::move(fetcher)), decompress_client_(decompress_client) {
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

void OffloadServerEpoll::start() { server_.start(); }
void OffloadServerEpoll::onConnected(const RdmaConnectionPtr &conn) {
  SPDLOG_INFO("OffloadServerEpoll connected");
}

void OffloadServerEpoll::onRecvSuccess(const RdmaConnectionPtr &conn,
                                       uint8_t *recv_buf, uint32_t recv_len,
                                       const ibv_wc &wc) {
  if (recv_len < sizeof(int)) {
    SPDLOG_ERROR("recv_len < 4");
    return;
  }
  int type = *reinterpret_cast<int *>(recv_buf);
  if (type == static_cast<int>(MsgType::kDecompressConnection)) {
    offload::DecompressConnectionRequest req{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        req)) {
      SPDLOG_ERROR("parse DecompressConnectionRequest error");
      return;
    }
    if (!handleDecompressConnectionReqeust(conn, req)) {
      SPDLOG_ERROR("handle DecompressConnectionRequest error");
      return;
    }
  } else if (type == static_cast<int>(MsgType::kOffload)) {
    offload::OffloadRequest req{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        req)) {
      SPDLOG_ERROR("parse OffloadRequest error");
      return;
    }
    if (!handleOffloadRequest(conn, req)) {
      SPDLOG_ERROR("handle OffloadRequest error");
      return;
    }
  } else {
    SPDLOG_ERROR("Request type error");
  }
}

void OffloadServerEpoll::onRecvFail(const RdmaConnectionPtr &conn,
                                    const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA recv complete fail. wr_id: {}", wc.wr_id);
}

void OffloadServerEpoll::onSendCompleteSuccess(const RdmaConnectionPtr &conn,
                                               const ibv_wc &wc) {}

void OffloadServerEpoll::onSendCompleteFail(const RdmaConnectionPtr &conn,
                                            const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA send complete fail. wr_id: {}", wc.wr_id);
}

bool OffloadServerEpoll::handleOffloadRequest(
    const RdmaConnectionPtr &conn, const offload::OffloadRequest &req) {
  /// you can modify this code to support query database and use cache
  auto &image = req.image_name_tag();
  int fetch_count = 1;

  for (auto &layer : req.layers()) {
    SPDLOG_DEBUG("image {}, layer {}", image, layer.layer());
    RdmaConfig rdmaConfig = {
        FLAGS_content_client_ib_dev_name,
        FLAGS_content_client_ib_dev_port,
        FLAGS_content_client_rdma_mem,
        FLAGS_content_client_rdma_mem_num
    };

    fetcher_->fetch(
        layer.layer(), image,
        InetAddress{FLAGS_content_client_peer_ip,
                    static_cast<uint16_t>(FLAGS_content_client_peer_port)},
        rdmaConfig);
  }

  // send response
  offload::OffloadResponse offload_resp{};
  offload_resp.set_image_name_tag(image);
  offload_resp.set_success(true);
  auto free_buf = conn->acquireFreeSendBuf();
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;

  *reinterpret_cast<int *>(send_buf) = static_cast<int>(MsgType::kOffload);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), offload_resp);
  if (frame_len == -1) {
    SPDLOG_ERROR("Serialize RDMA msg error");
    return false;
  }
  conn->send(send_buf, frame_len + sizeof(MsgType), wr_id_++);
  conn->releaseSendBuf(free_buf->id);
  SPDLOG_INFO("Send OffloadResponse. image: {} {}. wr_id {}",
              offload_resp.image_name_tag(),
              offload_resp.success() ? "success" : "fail", wr_id_ - 1);
  return true;
}

bool OffloadServerEpoll::handleDecompressConnectionReqeust(
    const RdmaConnectionPtr &conn,
    const offload::DecompressConnectionRequest &req) {
  SPDLOG_INFO("Recv DecompressConnectionRequest");
  offload::DecompressConnectionResponse resp{};
  resp.set_connection(true);
  auto free_buf = conn->acquireFreeSendBuf();
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;
  *reinterpret_cast<int *>(send_buf) =
      static_cast<int>(MsgType::kDecompressConnection);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), resp);
  if (frame_len == -1) {
    SPDLOG_ERROR("Serialize RDMA msg error");
    return false;
  }
  conn->send(send_buf, frame_len + sizeof(MsgType), wr_id_++);
  conn->releaseSendBuf(free_buf->id);
  SPDLOG_INFO("Send DecompressConnectionResponse. wr_id {}", wr_id_ - 1);
  decompress_client_->connect();
  return true;
}

} // namespace dpu
} // namespace hdc