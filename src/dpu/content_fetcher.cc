#include "content.pb.h"
#include "network/EventLoop.h"
#include "network/EventLoopThread.h"
#include "network/InetAddress.h"
#include "network/rdma/RdmaConfig.h"
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <dpu/content_fetcher.h>
#include <future>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>
#include <utils/MsgFrame.h>
using hdc::dpu::ContentFetcherPtr;
using hdc::dpu::ContentTaskQueue;
using hdc::dpu::ContentTaskQueuePtr;
using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaConfig;

namespace hdc {
namespace dpu {

ContentClient::ContentClient(EventLoop *loop, const InetAddress &listenAddr,
                             const std::string &name, RdmaConfig rdmaConfig,
                             DecompressClientEpoll *decompress_client,
                             std::shared_ptr<BlobPool> blob_pool)
    : client_(loop, listenAddr, name, std::move(rdmaConfig)), loop_(loop),
      decompress_client_(decompress_client), blob_pool_(std::move(blob_pool)) {
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

void ContentClient::connect() {
  loop_->assertInLoopThread();
  client_.connect();
}

void ContentClient::onConnected(const RdmaConnectionPtr &conn) {
  SPDLOG_DEBUG("ContentClient RDMA connection success");
  conn_ = conn;
  assert(!unsend_reqs_.empty() && !conn_->isFreeSendBufEmpty());
  // Send request
  trySendRequests();
}

void ContentClient::onRecvSuccess(const RdmaConnectionPtr &conn,
                                  uint8_t *recv_buf, uint32_t recv_len,
                                  const ibv_wc &wc) {
  // Get response and send segment to Decompress Client
  content::GetLayerResponse resp{};
  auto frame_len = parseRdmaPbMsg(recv_buf, recv_len, resp);

  conn->releaseSendBuf(inflight_sends_.front());
  inflight_sends_.pop_front();
  if (frame_len == -1) {
    SPDLOG_ERROR("parseRdmaPbMsg error");
  }
  assert(resp.segment_size() == recv_len - frame_len);

  size_t segment_size = recv_len - frame_len;
  Blob segment = blob_pool_->acquireBlob(segment_size);
  memcpy(segment.get_addr(), recv_buf + frame_len, segment_size);
  segment.set_size(segment_size);

  // record transfer time
  auto now = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration<double, std::micro>(now - start_time_).count();
  if (resp.image_name_tag() != curr_image_tag_) {
    rdma_duration_ = 0;
    curr_image_tag_ = resp.image_name_tag();
  }
  rdma_duration_ += duration;
  decompress_client_->submitDecompressTask(
      ContentElement{std::move(segment), resp.index(), resp.total_segments(),
                     resp.layer(), resp.image_name_tag(), resp.iscompressed()});

  SPDLOG_INFO("Recv GetLayerResponse: image: {}, layer: {}, index: {}, size: "
              "{}, rdma_rtt {}us, rdma_duration {}us",
              resp.image_name_tag(), resp.layer(), resp.index(),
              resp.segment_size(), duration, rdma_duration_);

  // Send new request if needed.
  if (int n = resp.total_segments(); resp.index() == 0 && n > 1) {
    for (int i = n - 1; i > 0; --i) {
      content::GetLayerRequest req{};
      req.set_layer(resp.layer());
      req.set_image_name_tag(resp.image_name_tag());
      req.set_index(i);
      req.set_total_segments(n);
      unsend_reqs_.push_front(std::move(req));
    }
  }

  trySendRequests();
}

void ContentClient::onRecvFail(const RdmaConnectionPtr &conn,
                               const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA recv fail");
}

void ContentClient::onSendCompleteSuccess(const RdmaConnectionPtr &conn,
                                          const ibv_wc &wc) {
  // SPDLOG_INFO("RDMA send complete success, wc_id {}, size {}", wc.wr_id,
  //             wc.byte_len);
}

void ContentClient::onSendCompleteFail(const RdmaConnectionPtr &conn,
                                       const ibv_wc &wc) {
  SPDLOG_ERROR("RDMA send complete fail");
}

void ContentClient::trySendRequests() {
  if (conn_ == nullptr) {
    return;
  }
  while (!unsend_reqs_.empty() && !conn_->isFreeSendBufEmpty()) {
    sendRequest();
  }
}

void ContentClient::enqueueRequest(content::GetLayerRequest req) {
  unsend_reqs_.emplace_back(std::move(req));
}

/// Before call this function, must assert both the unsend request queue and
/// RDMA free bufpair are not empty.
void ContentClient::sendRequest() {
  if (conn_ == nullptr) {
    return;
  }
  start_time_ = std::chrono::high_resolution_clock::now();

  auto free_buf = conn_->acquireFreeSendBuf();
  auto send_buf = free_buf->addr;
  auto send_cap = free_buf->cap;
  auto req = std::move(unsend_reqs_.front());
  unsend_reqs_.pop_front();
  auto frame_len = serializeRdmaPbMsg(send_buf, send_cap, req);
  if (frame_len == -1) {
    SPDLOG_ERROR("serializeRdmaPbMsg error");
    return;
  }
  start_time_ = std::chrono::high_resolution_clock::now();
  SPDLOG_INFO("Send GetLayerRequest. image_name_tag: {}, layer: {}, index: {}. "
              "wr_id: {}. free_bufpair {}",
              req.image_name_tag(), req.layer(), req.index(), wr_id_,
              free_buf->id);
  conn_->send(send_buf, frame_len, wr_id_++);
  inflight_sends_.emplace_back(free_buf->id);
}

ContentFetcher::ContentFetcher(EventLoop *loop,
                               DecompressClientEpoll *decompress_client)
    : loop_(loop), decompress_client_(decompress_client) {
  blob_pool_ = decompress_client_->get_blob_pool();
}

void ContentFetcher::fetch(const std::string &layer,
                           const std::string &image_name_tag,
                           const InetAddress &addr,
                           const RdmaConfig &rdma_config) {
  loop_->runInLoop([this, layer, image_name_tag, addr, rdma_config]() {
    loop_->assertInLoopThread();
    content::GetLayerRequest req{};
    req.set_layer(layer);
    req.set_image_name_tag(image_name_tag);
    req.set_index(0);
    req.set_total_segments(0);
    auto it = clients_.find(addr);
    if (it == clients_.end()) {
      auto client = std::make_unique<ContentClient>(
          loop_, addr, "ContentClient", std::move(rdma_config),
          decompress_client_, this->blob_pool_);
      client->enqueueRequest(std::move(req));
      client->connect();
      clients_.emplace(addr, std::move(client));
    } else {
      it->second->enqueueRequest(std::move(req));
      it->second->trySendRequests();
    }
  });
}
void ContentFetcher::loop() { loop_->loop(); }

} // namespace dpu
} // namespace hdc
