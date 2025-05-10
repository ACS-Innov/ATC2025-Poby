#include "container.pb.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaClient.h"
#include "network/rdma/RdmaConnection.h"
#include "offload.pb.h"
#include <cassert>
#include <cstdint>
#include <host/client/offload_client_epoll.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <utils/MsgFrame.h>

namespace hdc::host::client {
OffloadClientEpoll::OffloadClientEpoll(EventLoop *loop,
                                       const InetAddress &listen_addr,
                                       RdmaConfig rdmaConfig,
                                       std::string metadata_path)
    : client_(loop, listen_addr, "OffloadClient", std::move(rdmaConfig)),
      loop_(loop), metadata_path_(std::move(metadata_path)) {

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
}

void OffloadClientEpoll::connect() { client_.connect(); }

void OffloadClientEpoll::onConnected(const RdmaConnectionPtr &conn) {
  SPDLOG_INFO("OffloadClient connected");
  conn_ = conn;

  offload::DecompressConnectionRequest req{};
  req.set_connection(true);
  auto free_buf = conn->acquireFreeSendBuf();
  inflight_sends_.emplace_back(free_buf->id);
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;

  *reinterpret_cast<int *>(send_buf) =
      static_cast<int>(MsgType::kDecompressConnection);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), req);
  if (frame_len == -1) {
    SPDLOG_ERROR("Serialize RDMA msg error");
    return;
  }
  SPDLOG_INFO("send DecompressConnectionRequest. wr_id {}", wr_id_);
  conn->send(send_buf, frame_len + sizeof(MsgType), wr_id_++);
}

void OffloadClientEpoll::onRecvSuccess(const RdmaConnectionPtr &conn,
                                       uint8_t *recv_buf, uint32_t recv_len,
                                       const ibv_wc &wc) {
  if (recv_len < sizeof(int)) {
    SPDLOG_ERROR("recv_len < 4");
    return;
  }

  conn_->releaseSendBuf(inflight_sends_.front());
  inflight_sends_.pop_front();

  int type = *reinterpret_cast<int *>(recv_buf);
  if (type == static_cast<int>(MsgType::kDecompressConnection)) {
    offload::DecompressConnectionResponse resp{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        resp)) {
      SPDLOG_ERROR("parse DecompressConnectionResponse error");
      return;
    }
    if (!handleDecompressConnectionResponse(resp)) {
      SPDLOG_ERROR("handle DecompressConnectionResponse error");
      return;
    }
  } else if (type == static_cast<int>(MsgType::kOffload)) {
    offload::OffloadResponse resp{};
    if (!parseRdmaPbMsg(recv_buf + sizeof(MsgType), recv_len - sizeof(MsgType),
                        resp)) {
      SPDLOG_ERROR("parse OffloadResponse error");
      return;
    }
    if (!handleOffloadResponse(resp)) {
      SPDLOG_ERROR("handlre OffloadResponse error");
      return;
    }
  } else {
    SPDLOG_ERROR("Request type error");
  }
}

void OffloadClientEpoll::completeTask(UntarResult untar_res) {
  loop_->runInLoop([this, untar_res = std::move(untar_res)]() {
    loop_->assertInLoopThread();
    auto it_tasks = tasks_.find(untar_res.image_name_tag);
    assert(it_tasks != tasks_.end());
    auto &task_info = it_tasks->second;
    task_info.untar_layers++;
    if (task_info.untar_layers == task_info.total_layers) {
      // success
      tasks_.erase(it_tasks);
      container::CreateContainerResponse response{};
      response.set_path("untar/" + untar_res.image_name_tag);
      response.set_success(true);
      response.set_duration(0);
      sendTcpPbMsg(task_info.conn, response);
      SPDLOG_INFO("Send CreateContainerResponse. image: {}, path: {}, {}",
                  untar_res.image_name_tag, response.path(),
                  response.success() ? "success" : "fail");
    }
  });
}

void OffloadClientEpoll::onRecvFail(const RdmaConnectionPtr &conn,
                                    const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA recv complete fail. wr_id: {}", wc.wr_id);
}

void OffloadClientEpoll::onSendCompleteSuccess(const RdmaConnectionPtr &conn,
                                               const ibv_wc &wc) {
  // SPDLOG_DEBUG("RDMA send complete wr_id {}", wc.wr_id);
}

void OffloadClientEpoll::onSendCompleteFail(const RdmaConnectionPtr &conn,
                                            const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA send complete fail. wr_id: {}", wc.wr_id);
}

bool OffloadClientEpoll::handleOffloadResponse(
    const offload::OffloadResponse &resp) {
  SPDLOG_INFO("Recv OffloadResponse: image: {} {}", resp.image_name_tag(),
              resp.success() ? "success" : "fail");
  return tryOffloadTask(conn_);
}
bool OffloadClientEpoll::handleDecompressConnectionResponse(
    const offload::DecompressConnectionResponse &resp) {
  SPDLOG_INFO("Recv DecompressConnectionResponse");
  connected_ = true;

  if (!tryOffloadTask(conn_)) {
    return false;
  }
  return true;
}
bool OffloadClientEpoll::loadMetadata(const ImageNameTag &image) {
  if (metadatas_.find(image) != metadatas_.end()) {
    return true;
  }

  std::string name = image.id().substr(0, image.id().find(":"));
  std::string manifest_path = metadata_path_ + "/" + name + ":latest/manifest.json";
  auto manifest = OciManifest::from_file(manifest_path);
  if (!manifest.has_value()) {
    return false;
  }

  std::string config_path = metadata_path_ + "/" + name + ":latest/config";
  auto config = OciConfig::from_file(config_path);
  if (!config.has_value()) {
    return false;
  }
  metadatas_.emplace(
      image, ImageMetadata{image, std::move(*config), std::move(*manifest)});
  return true;
}

void OffloadClientEpoll::offload(OffloadElement task) {
  loop_->runInLoop([this, task = std::move(task)]() {
    loop_->assertInLoopThread();
    pending_tasks_.emplace_back(std::move(task));
    if (!connected_) {
      return;
    }
    if (!this->tryOffloadTask(conn_)) {
      SPDLOG_ERROR("Do offloadTask error");
    }
  });
}

bool OffloadClientEpoll::tryOffloadTask(const RdmaConnectionPtr &conn) {
  if (pending_tasks_.empty()) {
    return true;
  }
  auto free_buf = conn->acquireFreeSendBuf();
  if (!free_buf.has_value()) {
    return true;
  }
  auto &task = pending_tasks_.front();
  if (!offloadTaskToDpu(conn_, task, *free_buf)) {
    SPDLOG_ERROR("Do offloadTask error");
    pending_tasks_.pop_front();
    return false;
  }
  pending_tasks_.pop_front();
  return true;
}
bool OffloadClientEpoll::offloadTaskToDpu(
    const RdmaConnectionPtr &conn, const OffloadElement &task,
    const RdmaConnection::SendBuf &free_buf) {
  // load metadata
  ImageNameTag image{task.image_name_tag};
  if (!loadMetadata(image)) {
    SPDLOG_ERROR("loadMetadata error");
    return false;
  }
  auto &metadata = metadatas_[image];
  auto &manifest = metadata.manifest();
  size_t layers_len = manifest.layers_len();

  // // update tasks and layers.
  // // construct OffloadRequest.
  offload::OffloadRequest offload_req{};
  offload_req.set_image_name_tag(image.id());

  for (size_t i = 0; i < layers_len; ++i) {
    auto m_layer = manifest.layers()[i];
    std::string layer_name = std::string{m_layer->digest};
    offload::LayerElement layer;
    layer.set_layer(m_layer->digest);
    *offload_req.add_layers() = layer;
  }
  tasks_.emplace(image, TaskInfo{static_cast<int>(layers_len), 0, task.conn});

  // send OffloadRequest
  inflight_sends_.emplace_back(free_buf.id);
  auto send_buf = free_buf.addr;
  auto send_cap = free_buf.cap;

  *reinterpret_cast<int *>(send_buf) = static_cast<int>(MsgType::kOffload);
  auto frame_len = serializeRdmaPbMsg(send_buf + sizeof(MsgType),
                                      send_cap - sizeof(MsgType), offload_req);
  if (frame_len == -1) {
    SPDLOG_ERROR("Serialize RDMA msg error");
    return false;
  }
  conn->send(send_buf, frame_len + sizeof(MsgType), wr_id_++);
  SPDLOG_INFO("Send OffloadRequest. image {}. wr_id {}",
              offload_req.image_name_tag(), wr_id_ - 1);
  return true;
}
} // namespace hdc::host::client