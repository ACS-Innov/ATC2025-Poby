#pragma once
#include "dpu/decompress_client_epoll.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaServer.h"
#include "offload.pb.h"
#include <dpu/content_fetcher.h>
#include <dpu/metadata.h>
#include <spdlog/spdlog.h>
#include <utils/MsgFrame.h>

namespace hdc {
namespace dpu {
using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaConfig;
using hdc::network::rdma::RdmaServer;
class OffloadServerEpoll {

public:
  OffloadServerEpoll(EventLoop *loop, const InetAddress &listen_addr,
                     RdmaConfig rdma_config, ContentFetcherPtr fetcher,
                     DecompressClientEpoll *decompress_client) noexcept;

  void start();

private:
  enum class MsgType : int { kDecompressConnection, kOffload };
  RdmaServer server_;
  ContentFetcherPtr fetcher_;
  DecompressClientEpoll *decompress_client_;
  uint64_t wr_id_{0};

  void onConnected(const RdmaConnectionPtr &conn);

  void onRecvSuccess(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                     uint32_t recv_len, const ibv_wc &wc);

  void onRecvFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);
  void onSendCompleteSuccess(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  bool handleOffloadRequest(const RdmaConnectionPtr &conn,
                            const offload::OffloadRequest &req);

  bool handleDecompressConnectionReqeust(
      const RdmaConnectionPtr &conn,
      const offload::DecompressConnectionRequest &req);
};

} // namespace dpu
} // namespace hdc