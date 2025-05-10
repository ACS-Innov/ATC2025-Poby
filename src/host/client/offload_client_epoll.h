#pragma once
#include "host/client/metadata.h"
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaClient.h"
#include "network/rdma/RdmaConfig.h"
#include "network/rdma/RdmaConnection.h"
#include "network/tcp/Callbacks.h"
#include "offload.pb.h"
#include <deque>
#include <host/client/metadata.h>
#include <map>
#include <network/tcp/TcpConnection.h>
#include <set>
namespace hdc {
namespace host {
namespace client {
using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaClient;
using hdc::network::rdma::RdmaConfig;
using hdc::network::rdma::RdmaConnectionPtr;
using hdc::network::tcp::TcpConnectionPtr;
using hdc::network::rdma::RdmaConnection;
class OffloadClientEpoll {
public:
  OffloadClientEpoll(EventLoop *loop, const InetAddress &listenAddr,
                     RdmaConfig rdmaConfig, std::string metadata_path);

  /// @brief: Try to offload a task to DPU. thread safe.
  void offload(OffloadElement task);

  /// @brief: It will be called when a fetch->decompress->untar task is
  /// completed. It will send a response to CommandClient. Thread safe.
  void completeTask(UntarResult untar_res);

  void connect();

private:
  struct TaskInfo {
    int total_layers{0};
    int untar_layers{0};
    TcpConnectionPtr conn{nullptr};
    TaskInfo(int total_layers, int untar_layers, TcpConnectionPtr conn)
        : total_layers(total_layers), untar_layers(untar_layers),
          conn(std::move(conn)) {}
    TaskInfo() {}

    TaskInfo(const TaskInfo &) = default;

    TaskInfo &operator=(const TaskInfo &) = default;

    TaskInfo(TaskInfo &&) = default;

    TaskInfo &operator=(TaskInfo &&) = default;
  };
  enum class MsgType : int { kDecompressConnection, kOffload };
  using TaskMap = std::map<ImageNameTag, TaskInfo>;
  // a layer may be used by multiple images.
  using LayerMap = std::map<std::string, std::set<ImageNameTag>>;
  using MetadataMap = std::map<ImageNameTag, ImageMetadata>;

  RdmaClient client_;
  EventLoop *loop_;
  std::deque<OffloadElement> pending_tasks_;
  // record the bufpair_id of inflight_sends_;
  std::deque<size_t> inflight_sends_;
  std::string metadata_path_;
  bool connected_{false};
  RdmaConnectionPtr conn_{nullptr};
  TaskMap tasks_;
  // LayerMap layers_;
  MetadataMap metadatas_;
  uint64_t wr_id_{0};

  void onConnected(const RdmaConnectionPtr &conn);

  void onRecvSuccess(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                     uint32_t recv_len, const ibv_wc &wc);

  void onRecvFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteSuccess(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  bool handleOffloadResponse(const offload::OffloadResponse &resp);

  bool handleDecompressConnectionResponse(
      const offload::DecompressConnectionResponse &resp);

  /// @brief: Loads metadata to metadatas_. Return true if metadata is already
  /// in the map or is loaded successfully. Otherwise returns false.
  bool loadMetadata(const ImageNameTag &image);

  /// @brief: Offload the task to DPU. It will query metadata and send request
  /// to DPU. Not thread safe.
  bool offloadTaskToDpu(const RdmaConnectionPtr &conn,
                        const OffloadElement &task, const RdmaConnection::SendBuf &free_buf);
  bool tryOffloadTask(const RdmaConnectionPtr &conn);
};
} // namespace client
} // namespace host
} // namespace hdc