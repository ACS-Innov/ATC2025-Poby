#pragma once
#include "content.pb.h"
#include "dpu/decompress_client_epoll.h"
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaClient.h"
#include "network/rdma/RdmaConfig.h"
#include "utils/blob_pool.h"
#include <chrono>
#include <deque>
#include <folly/concurrency/UnboundedQueue.h>
#include <future>
#include <map>
#include <memory>
#include <optional>

namespace hdc {
namespace dpu {
using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaClient;
using hdc::network::rdma::RdmaConfig;
using hdc::network::rdma::RdmaConnectionPtr;

using ContentTaskQueue = folly::USPSCQueue<ContentElement, false>;
using ContentTaskQueuePtr = std::shared_ptr<ContentTaskQueue>;

class ContentClient {
public:
  ContentClient(EventLoop *loop, const InetAddress &listenAddr,
                const std::string &name, RdmaConfig rdmaConfig,
                DecompressClientEpoll *decompress_client,
                std::shared_ptr<BlobPool> blob_pool);

  void connect();

  void trySendRequests();

  void enqueueRequest(content::GetLayerRequest req);

private:
  RdmaClient client_;
  EventLoop *loop_;
  DecompressClientEpoll *decompress_client_;
  std::shared_ptr<BlobPool> blob_pool_;

  uint64_t wr_id_{0};
  std::deque<content::GetLayerRequest> unsend_reqs_;
  RdmaConnectionPtr conn_{nullptr};
  // record the bufpair_id of inflight_sends_;
  std::deque<size_t> inflight_sends_;

  // record the image translate time without pipeline.
  std::chrono::high_resolution_clock::time_point start_time_;
  double rdma_duration_{0};
  std::string curr_image_tag_{""};

  void onConnected(const RdmaConnectionPtr &conn);

  void onRecvSuccess(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                     uint32_t recv_len, const ibv_wc &wc);

  void onRecvFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteSuccess(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void sendRequest();
};

class ContentFetcher {
public:
  ContentFetcher(EventLoop *loop, DecompressClientEpoll *decompress_client);

  ContentFetcher(const ContentFetcher &) = delete;

  ContentFetcher &operator=(const ContentFetcher &) = delete;

  ContentFetcher(ContentFetcher &&) = delete;

  ContentFetcher &operator=(ContentFetcher &&) = delete;

  /// @brief: Fetch segments from remote. Thread Safe.
  void fetch(const std::string &layer, const std::string &image_name_tag,
             const InetAddress &addr, const RdmaConfig &rdma_config);

  void loop();

private:
  using ClientMap = std::map<InetAddress, std::unique_ptr<ContentClient>>;
  EventLoop *loop_;
  ClientMap clients_;
  DecompressClientEpoll *decompress_client_;
  std::shared_ptr<BlobPool> blob_pool_;
};
using ContentFetcherPtr = std::shared_ptr<ContentFetcher>;

} // namespace dpu
} // namespace hdc
