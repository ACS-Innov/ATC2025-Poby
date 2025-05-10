#pragma once

#include "compress.pb.h"
#include "doca/compress.h"
#include "host/client/untar_engine.h"
#include "network/rdma/RdmaServer.h"
#include <cstdint>
namespace hdc {
namespace host {
namespace client {

using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaConfig;
using hdc::network::rdma::RdmaConnectionPtr;
using hdc::network::rdma::RdmaServer;
class DecompressServerEpoll {
public:
  DecompressServerEpoll(CompressEngine compress_engine,
                        //  DmaEngine dma_engine,
                        EventLoop *loop, const InetAddress &listen_addr,
                        RdmaConfig rdma_config, size_t untar_num_threads,
                        std::string untar_file_path,
                        OffloadClientEpoll *offload_client) noexcept;

  DecompressServerEpoll(const DecompressServerEpoll &) = delete;

  DecompressServerEpoll &operator=(const DecompressServerEpoll &) = delete;

  DecompressServerEpoll(DecompressServerEpoll &&) = delete;

  DecompressServerEpoll &operator=(DecompressServerEpoll &&) = delete;

  void start();

private:
  enum class MsgType : int {
    kMmapInfo,
    kDecompressFinish,
  };

  CompressEngine compress_engine_;
  RdmaServer server_;
  UntarEngine untar_engine_;
  uint64_t wr_id_{0};

  void onConnected(const RdmaConnectionPtr &conn);

  void onRecvSuccess(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                     uint32_t recv_len, const ibv_wc &wc);

  void onRecvFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteSuccess(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  bool handleMmapInfoRequest(const RdmaConnectionPtr &conn,
                             const compress::MmapInfoRequest &req);

  bool
  handleDecompressFinishRequest(const RdmaConnectionPtr &conn,
                                const compress::DecompressFinishRequest &req,
                                uint8_t *remain_buf, int remain_len);
};

} // namespace client
} // namespace host
} // namespace hdc