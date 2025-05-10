#include "content.pb.h"
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaConfig.h"
#include "utils_file.h"
#include "utils_verify.h"
#include <cstddef>
#include <fstream>
#include <gflags/gflags.h>
#include <network/rdma/RdmaConnection.h>
#include <network/rdma/RdmaServer.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <utility>
#include <utils/MsgFrame.h>

using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaConfig;
using hdc::network::rdma::RdmaConnectionPtr;
using hdc::network::rdma::RdmaServer;

int get_int_from_file(std::string path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    SPDLOG_ERROR("Failed to open file {}", path);
    return -1;
  }
  int value;
  file >> value;
  if (file.fail()) {
    SPDLOG_ERROR("Failed to read integer from file {}", path);
    return -1;
  }
  return value;
}

DEFINE_string(content_server_ib_dev_name, "mlx5_0",
              "The IB device name of host ContentServer");
DEFINE_int32(content_server_ib_dev_port, 1,
             "The IB device port of host ContentServer");
DEFINE_string(content_server_listen_ip, "172.24.46.186",
              "The IP address of host ContentServer to listen");
DEFINE_uint32(content_server_listen_port, 9002,
              "The IP port of host ContentServer to listen");
DEFINE_uint64(
    content_server_rdma_mem, 129 * 1024 * 1024,
    "The memory of RDMA sendbuf/recvbuf for host ContentServer in bytes");
DEFINE_uint64(content_server_rdma_mem_num, 4,
              "The number of RDMA sendbuf/recvbuf");
DEFINE_string(content_server_registry_path, "data/registry/content_layers/",
              "Root path of image layers for registry");
DEFINE_bool(content_server_layer_is_compressed, true,
            "The layer is compressed or not");
class ContentServer {

public:
  ContentServer(EventLoop *loop, const InetAddress &listenAddr,
                const std::string &name, RdmaConfig rdmaConfig,
                const std::string &registry_path)
      : server_(loop, listenAddr, name, std::move(rdmaConfig)),
        registry_path_(registry_path) {
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
  void start() {
    SPDLOG_INFO("Content Server start");
    server_.start();
  }

  void onConnected(const RdmaConnectionPtr &conn) {
    SPDLOG_INFO("new RDMA connection");
  }

  void onRecvSuccess(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                     uint32_t recv_len, const ibv_wc &wc) {
    auto get_layer_req = content::GetLayerRequest();
    if (!parseRdmaPbMsg(recv_buf, recv_len, get_layer_req)) {
      SPDLOG_ERROR("Parse GetLayerRequest error");
      return;
    }
    SPDLOG_INFO(
        "Recv GetLayerRequest. image_name_tag: {}, layer: {}, index: {}",
        get_layer_req.image_name_tag(), get_layer_req.layer(),
        get_layer_req.index());
    auto &layer_digest = get_layer_req.layer();
    if (!util_valid_digest(layer_digest.c_str())) {
      SPDLOG_ERROR("GetLayerRequest layer id {} error, len {}, strlen {}",
                   layer_digest, layer_digest.length(),
                   strlen(layer_digest.c_str()));
    }
    auto index = get_layer_req.index();

    auto index_path = registry_path_ + "/" + layer_digest + "/" +
                      std::to_string(index) + ".tar.gz";
    if (!util_file_exists(index_path.c_str())) {
      SPDLOG_ERROR("Layer dir {} not exsit", index_path);
    }

    auto get_layer_resp = content::GetLayerResponse();
    get_layer_resp.set_layer(layer_digest);
    get_layer_resp.set_index(index);
    get_layer_resp.set_iscompressed(FLAGS_content_server_layer_is_compressed);
    auto total_segments_path =
        registry_path_ + "/" + layer_digest + "/total_segment.txt";
    get_layer_resp.set_total_segments(get_int_from_file(total_segments_path));
    auto segment_size = util_file_size(index_path.c_str());
    if (segment_size <= 0) {
      SPDLOG_ERROR("GetLayerRequest segment size is %d", segment_size);
      return;
    }
    get_layer_resp.set_segment_size(segment_size);
    get_layer_resp.set_image_name_tag(get_layer_req.image_name_tag());

    auto free_buf = conn->acquireFreeSendBuf();
    auto send_buf = free_buf->addr;
    auto send_cap = free_buf->cap;
    auto frame_len = serializeRdmaPbMsg(send_buf, send_cap, get_layer_resp);
    if (util_file2str(index_path.c_str(),
                      reinterpret_cast<char *>(send_buf) + frame_len,
                      segment_size + 1) < 0) {
      SPDLOG_ERROR("Read layer file {} failed, size : {}", index_path,
                   segment_size);
      return;
    }
     SPDLOG_DEBUG("read file end. size {}MB",1.0*segment_size / 1024/1024 );
    SPDLOG_INFO("Send GetLayerResponse. image: {}, layer: {}, idx: {}, "
                "total_segments: {}, size: {}",
                get_layer_resp.image_name_tag(), get_layer_resp.layer(),
                get_layer_resp.index(), get_layer_resp.total_segments(),
                get_layer_resp.segment_size());
    conn->send(send_buf, frame_len + segment_size, 0);
    conn->releaseSendBuf(free_buf->id);
  }

  void onRecvFail(const RdmaConnectionPtr &conn, const ibv_wc &wc) {
    SPDLOG_ERROR("RDMA recv fail");
  }

  void onSendCompleteSuccess(const RdmaConnectionPtr &conn, const ibv_wc &wc) {
    // SPDLOG_INFO("RDMA send complete success");
  }

  void onSendCompleteFail(const RdmaConnectionPtr &conn, const ibv_wc &wc) {
    SPDLOG_INFO("RDMA send complete fail");
  }

private:
  RdmaServer server_;
  const std::string registry_path_;
};

int main(int argc, char *argv[]) {
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("%^[%L][%T.%e]%$[%s:%#] %v");
  RdmaConfig rdmaConfig = {
      FLAGS_content_server_ib_dev_name, FLAGS_content_server_ib_dev_port,
      FLAGS_content_server_rdma_mem, FLAGS_content_server_rdma_mem_num};

  EventLoop loop;

  auto server = ContentServer{
      &loop,
      InetAddress{FLAGS_content_server_listen_ip,
                  static_cast<uint16_t>(FLAGS_content_server_listen_port)},
      "RdmaContentServer", rdmaConfig, FLAGS_content_server_registry_path};

  server.start();
  loop.loop();
}
