#pragma once
#include "network/Channel.h"
#include "network/Timestamp.h"
#include "network/rdma/RdmaClient.h"
#include "network/rdma/RdmaConnector.h"
#include "network/rdma/RdmaExchangeInfo.h"
#include "network/rdma/error.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <infiniband/verbs.h>
#include <memory>
#include <network/EventLoop.h>
#include <network/rdma/Callbacks.h>
#include <network/rdma/DevContext.h>
#include <network/rdma/RdmaServer.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <tl/expected.hpp>
#include <unistd.h>
#include <unordered_map>
#include <vector>
namespace hdc {
namespace network {
namespace rdma {

class RdmaConnection : public std::enable_shared_from_this<RdmaConnection> {
  template <class T> struct memalign_allocator {
    using value_type = T;
    T *allocate(std::size_t n) {
      void *p;
      if (posix_memalign(&p, sysconf(_SC_PAGESIZE), n * sizeof(T)))
        throw std::bad_alloc{};
      return static_cast<T *>(p);
    }
    void deallocate(T *p, std::size_t n) { free(p); }
    constexpr bool operator==(memalign_allocator) noexcept { return true; }
    constexpr bool operator!=(memalign_allocator) noexcept { return false; }
  };

  struct BufPair {
    uint64_t send_header;
    uint32_t send_cap;
    uint64_t recv_header;
    uint32_t recv_cap;
  };

public:
  struct ConnDest {
    // Local identifier
    uint16_t lid_;
    // Queue pair number
    uint32_t qpn_;
    // Packet sequence number
    uint32_t psn_;
    unsigned int gid_index_;
    // Global identifier
    union ibv_gid gid_;
    // GUID
    uint64_t guid_;
    // queue depth
    unsigned int rx_depth_;
    unsigned int tx_depth_;
  };

  struct SendBuf {
    size_t id{0};
    uint8_t *addr{nullptr};
    uint32_t cap{0};

    SendBuf() {}

    SendBuf(size_t id, uint8_t *addr, uint32_t cap)
        : id(id), addr(addr), cap(cap) {}
  };

  RdmaConnection(const RdmaConnection &) = delete;

  RdmaConnection &operator=(const RdmaConnection &) = delete;

  RdmaConnection(RdmaConnection &&) noexcept = delete;

  RdmaConnection &operator=(RdmaConnection &&) noexcept = delete;

  /// Construct a RdmaConnection with a connected RDMA connection.
  RdmaConnection(EventLoop *loop, const std::string &name,
                 ibv_comp_channel *comp_channel, ibv_pd *pd, ibv_cq *cq,
                 ibv_qp *qp, ibv_mr *mr,
                 std::vector<uint8_t, memalign_allocator<uint8_t>> local_mem,
                 uint32_t local_lkey, std::unique_ptr<DevContext> dev_ctx,
                 std::unique_ptr<ConnDest> local_dest,
                 std::unique_ptr<Channel> channel, int wc_capacity,
                 size_t mem_num) noexcept;

  ~RdmaConnection();

  inline const std::string &name() const { return name_; }

  inline bool connected() const & { return state_ == State::kConnected; }

  inline bool disconnected() const & { return state_ == State::kDisconnected; }

  // inline std::tuple<uint8_t *, uint32_t> get_send_buf() const & {
  //   return std::make_tuple(reinterpret_cast<uint8_t *>(send_header_),
  //                          static_cast<uint32_t>(send_capacity_));
  // }

  std::optional<SendBuf> acquireFreeSendBuf() {
    if (!free_bufpairs_.empty()) {
      size_t id = free_bufpairs_.front();
      free_bufpairs_.pop_front();
      return SendBuf{id, reinterpret_cast<uint8_t *>(bufpairs_[id].send_header),
                     bufpairs_[id].send_cap};
    } else {
      return std::nullopt;
    }
  }

  void releaseSendBuf(size_t bufpair_id) {
    free_bufpairs_.emplace_back(bufpair_id);
  }

  bool isFreeSendBufEmpty() { return free_bufpairs_.empty(); }

  static tl::expected<RdmaConnectionPtr, RDMAError>
  create(std::string_view ib_dev_name, int ib_dev_port, size_t mem_size,
         size_t mem_num, EventLoop *loop, const std::string &name);

  /// @brief: Thread Safe.
  void send(const void *addr, uint32_t length, uint64_t wr_id) &;

  inline void setRecvSuccessCallback(const RecvSuccessCallback &cb) {
    recvSuccessCallback_ = cb;
  }

  inline void setRecvFailCallback(const RecvFailCallback &cb) {
    recvFailCallback_ = cb;
  }

  inline void
  setSendCompleteSuccessCallback(const SendCompleteSuccessCallback &cb) {
    sendCompleteSuccessCallback_ = cb;
  }

  inline void setSendCompleteFailCallback(const SendCompleteFailCallback &cb) {
    sendCompleteFailCallback_ = cb;
  }

  inline void setConnectedCallback(const ConnectedCallback &cb) {
    connectedCallback_ = cb;
  }

  inline void setDisconnectedCallback(const DisconnectedCallback &cb) {
    disconnectedCallback_ = cb;
  }

  inline void callConnectedCallback() & {
    connectedCallback_(shared_from_this());
  }

private:
  friend class RdmaConnector;
  friend class RdmaClient;
  friend class RdmaServer;
  enum class State {
    kDisconnected,
    kConnected,
    kConnecting,
  };

  static constexpr int kUnackCqEventThreshold = 10;
  /// @brief:It is called when RdmaClient/RdmaServer establish a new
  /// RdmaConnection. It should be called only once.
  /// @detail: It will add Channel to the eventLoop(Poller), but it won't call
  /// user defined connectedCallback, due to tht fact that peer side may not
  /// ready to receive.

  void connectEstablished() &;

  /// @brief: called when RdmaConnector has removed me from its map. It should
  /// be called only once.
  void connectDestroyed() &;

  inline void setState(State state) { state_ = state; }

  /// @brief: Post send in the EventLoop thread.
  RDMAError sendInLoop(const void *addr, uint32_t length, uint64_t wr_id) &;

  inline RDMAError postSend(uint64_t addr, uint32_t length, uint64_t wr_id = 0,
                            unsigned int send_flags = IBV_SEND_SIGNALED) & {
    // SPDLOG_DEBUG("post send addr {}, length {}, wr_id {}", addr, length, wr_id);
    // Create a stringstream to store the hex representation
    //   std::stringstream ss;
    // const uint8_t* data = reinterpret_cast<uint8_t*>(addr);
    //   for (size_t i = 0; i < length; ++i) {
    //       ss << static_cast<char>(data[i]);
    //   }
    // SPDLOG_DEBUG("data {}", ss.str());
    struct ibv_sge list;
    list.addr = addr;
    list.length = length;
    list.lkey = local_lkey_;
    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = wr_id;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = send_flags;

    struct ibv_send_wr *bad_wr;
    int errnum = ibv_post_send(qp_, &wr, &bad_wr);
    if (errnum != 0) {
      SPDLOG_ERROR("Post send failed: {}", strerror(errnum));
      return RDMAError::kQpError;
    }
    return RDMAError::kSuccess;
  }

  inline RDMAError postRecv(size_t bufpair_id) {
    auto &buf = bufpairs_[bufpair_id];
    return postRecv(buf.recv_header, buf.recv_cap, bufpair_id);
  }

  inline RDMAError postRecv(uint64_t addr, uint32_t length,
                            uint64_t wr_id = 0) & {
    // SPDLOG_DEBUG("postRecv addr {}, length {}, wr_id {}", addr, length,
    // wr_id);
    ibv_sge list;
    ibv_recv_wr wr;
    memset(&wr, 0, sizeof(wr));
    list.addr = addr;
    list.length = length;
    list.lkey = local_lkey_;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.wr_id = wr_id;

    struct ibv_recv_wr *bad_wr;
    int errnum = ibv_post_recv(qp_, &wr, &bad_wr);
    if (errnum != 0) {
      SPDLOG_ERROR("Post receive failed: {}", strerror(errnum));
      return RDMAError::kQpError;
    }
    return RDMAError::kSuccess;
  }

  inline RDMAError fillRq() {
    for (size_t i = 0, n = bufpairs_.size(); i < n; ++i) {
      auto err = postRecv(i);
      if (err != RDMAError::kSuccess) {
        return err;
      }
    }
    return RDMAError::kSuccess;
  }

  /// @brief: Register to Channel for handling read Event.
  void handleRead(Timestamp recvTime);

  void handleWrite();

  void handleClose();

  void handleError();

  RDMAError connectQp(const RdmaExchangeInfo &remoteInfo) &;

  EventLoop *loop_;
  State state_{State::kConnecting};
  std::unique_ptr<Channel> channel_;
  RecvSuccessCallback recvSuccessCallback_;
  RecvFailCallback recvFailCallback_;
  SendCompleteSuccessCallback sendCompleteSuccessCallback_;
  SendCompleteFailCallback sendCompleteFailCallback_;
  ConnectedCallback connectedCallback_;
  DisconnectedCallback disconnectedCallback_;
  std::string name_;
  // Completion channel
  ibv_comp_channel *comp_channel_;
  // Protection domain
  ibv_pd *pd_;
  // Completion queue
  ibv_cq *cq_;
  // Queue pair
  ibv_qp *qp_;
  // Memory region
  ibv_mr *mr_;
  // The local memory for RDMA
  std::vector<uint8_t, memalign_allocator<uint8_t>> local_mem_;
  std::vector<BufPair> bufpairs_;
  std::deque<size_t> free_bufpairs_;
  uint32_t local_lkey_;
  // The remote memory for RPC
  uint64_t remote_mem_;
  uint32_t remote_len_;
  uint32_t remote_rkey_;
  std::unique_ptr<DevContext> dev_ctx_;
  std::unique_ptr<ConnDest> local_dest_;
  int unack_cq_events_{0};
  int wc_capacity_;
  std::vector<ibv_wc> wcs_;
};

} // namespace rdma
} // namespace network
} // namespace hdc