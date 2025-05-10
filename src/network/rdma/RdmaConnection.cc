#include "network/Channel.h"
#include "network/rdma/Callbacks.h"
#include "network/rdma/error.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <memory>
#include <network/EventLoop.h>
#include <network/rdma/RdmaConnection.h>
#include <spdlog/spdlog.h>

namespace hdc {
namespace network {
namespace rdma {
tl::expected<RdmaConnectionPtr, RDMAError>
RdmaConnection::create(std::string_view ib_dev_name, int ib_dev_port,
                       size_t mem_size, size_t mem_num, EventLoop *loop,
                       const std::string &name) {
  auto e_dev_context = DevContext::create(ib_dev_name, ib_dev_port);
  auto result = RDMAError::kSuccess;
  ibv_comp_channel *comp_channel = nullptr;
  ibv_pd *pd = nullptr;
  ibv_cq *cq = nullptr;
  int access_flags =
      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;
  ibv_mr *mr = nullptr;
  uint32_t local_lkey = 0;
  ibv_qp *qp = nullptr;
  auto local_dest = std::make_unique<ConnDest>();
  int errnum = 0;
  unsigned int tx_depth = mem_num + 5;
  unsigned int rx_depth = mem_num + 5;
  unsigned int wc_capacity = tx_depth + rx_depth;
  // allocate local memory which is aligned by PAGE_SIZE
  mem_size = [mem_size]() -> auto{
    if (mem_size % sysconf(_SC_PAGE_SIZE) == 0) {
      return mem_size;
    } else {
      return sysconf(_SC_PAGE_SIZE) * (mem_size / sysconf(_SC_PAGE_SIZE) + 1);
      ;
    }
  }
  ();
  mem_size = mem_size * mem_num * 2;
  std::vector<uint8_t, memalign_allocator<uint8_t>> local_mem(mem_size, 0);

  if (!e_dev_context.has_value()) {
    SPDLOG_ERROR("create dev_context error");
    return tl::unexpected(e_dev_context.error());
  }
  auto dev_context = std::move(*e_dev_context);

  comp_channel = ibv_create_comp_channel(dev_context->ctx_);
  if (!comp_channel) {
    errnum = errno;
    SPDLOG_ERROR("Cannot create completion channel: {}", strerror(errnum));
    result = RDMAError::kCompChannelError;
    goto fail_create_comp_channel;
  }

  int flags;
  int ret;
  /* change the blocking mode of the completion channel */
  flags = fcntl(comp_channel->fd, F_GETFL);
  errnum = fcntl(comp_channel->fd, F_SETFL, flags | O_NONBLOCK);
  if (errnum < 0) {
    SPDLOG_ERROR("Fail to change comp_channel NONBLOCK");
    result = RDMAError::kCompChannelError;
    goto clean_cq;
  }

  // Allocate protection domain
  pd = ibv_alloc_pd(dev_context->ctx_);
  if (!pd) {
    errnum = errno;
    SPDLOG_ERROR("Fail to allocate protection domain: {}", strerror(errnum));
    result = RDMAError::kPdError;
    goto fail_create_pd;
  }

  // Query device attributes
  errnum = ibv_query_device(dev_context->ctx_, &(dev_context->dev_attr_));
  if (errnum != 0) {
    SPDLOG_ERROR("Fail to query device attributes: {}", strerror(errnum));
    result = RDMAError::kDeviceInfoError;
    goto clean_pd;
  }

  // Query port attributes
  errnum = ibv_query_port(dev_context->ctx_, dev_context->ib_dev_port_,
                          &(dev_context->port_attr_));
  if (errnum != 0) {
    SPDLOG_ERROR("Fail to query port attributes: {}", strerror(errnum));
    result = RDMAError::kDeviceInfoError;
    goto clean_pd;
  }

  // Create a completion queue

  // Check limitation of tx_depth and rx_depth
  {
    unsigned int tx_depth_limit =
        (unsigned int)(dev_context->dev_attr_.max_qp_wr) / 4;
    if (tx_depth > tx_depth_limit) {
      SPDLOG_ERROR("TX depth {} > limit {}", tx_depth, tx_depth_limit);
      result = RDMAError::kQpError;
      goto clean_pd;
    }
  }
  {
    unsigned int rx_depth_limit =
        (unsigned int)(dev_context->dev_attr_.max_qp_wr) / 4;
    if (rx_depth > rx_depth_limit) {
      SPDLOG_ERROR("RX depth {} > limit {}", rx_depth, rx_depth_limit);
      result = RDMAError::kQpError;
      goto clean_pd;
    }
  }
  local_dest->tx_depth_ = tx_depth;
  local_dest->rx_depth_ = rx_depth;
  cq = ibv_create_cq(dev_context->ctx_, rx_depth + tx_depth, NULL, comp_channel,
                     0);
  if (!cq) {
    errnum = errno;
    SPDLOG_ERROR("Fail to create the completion queue: {}", strerror(errnum));
    result = RDMAError::kCqError;
    goto clean_pd;
  }

  errnum = ibv_req_notify_cq(cq, 0);
  if (errnum != 0) {
    SPDLOG_ERROR("Cannot request CQ notification: {}", strerror(errnum));
    result = RDMAError::kCompChannelError;
    goto clean_cq;
  }

  mr = ibv_reg_mr(pd, local_mem.data(), local_mem.size(), access_flags);
  if (!mr) {
    errnum = errno;
    SPDLOG_ERROR("Cannot register mr: {}", strerror(errnum));
    result = RDMAError::kMrError;
    goto clean_cq;
  }
  local_lkey = mr->lkey;

  // Create a queue pair (QP)
  struct ibv_qp_attr attr;
  {
    struct ibv_qp_init_attr init_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .cap =
            {
                .max_send_wr = tx_depth,
                .max_recv_wr = rx_depth,
                .max_send_sge = 1,
                .max_recv_sge = 1,
            },
        .qp_type = IBV_QPT_RC,
    };

    qp = ibv_create_qp(pd, &init_attr);
  }
  if (!qp) {
    errnum = errno;
    SPDLOG_ERROR("Fail to create QP: {}", strerror(errnum));
    result = RDMAError::kQpError;
    goto clean_mr;
  }

  attr.qp_state = IBV_QPS_INIT;
  attr.pkey_index = 0;
  attr.port_num = dev_context->ib_dev_port_;
  // Allow incoming RDMA writes on this QP
  attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;

  errnum = ibv_modify_qp(qp, &attr,
                         IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                             IBV_QP_ACCESS_FLAGS);
  if (errnum != 0) {
    SPDLOG_ERROR("Fail to modify QP to INIT: {}", strerror(errnum));
    result = RDMAError::kQpError;
    goto clean_qp;
  }

  srand48(getpid() * time(NULL));
  // local identifier

  local_dest->lid_ = dev_context->port_attr_.lid;
  // QP number
  local_dest->qpn_ = qp->qp_num;
  // packet sequence number
  local_dest->psn_ = lrand48() & 0xffffff;

  // global identifier
  local_dest->gid_index_ = dev_context->gid_index_list_[0];

  local_dest->gid_ = dev_context->gid_list_[0];

  local_dest->guid_ = dev_context->guid_;
  {

    auto channel =
        std::make_unique<Channel>(loop, comp_channel->fd, name + "_channel");

    auto conn = std::make_shared<RdmaConnection>(
        loop, name, comp_channel, pd, cq, qp, mr, std::move(local_mem),
        local_lkey, std::move(dev_context), std::move(local_dest),
        std::move(channel), wc_capacity, mem_num);
    return conn;
  }
clean_qp:
  ibv_destroy_qp(qp);
clean_mr:
  ibv_dereg_mr(mr);
clean_cq:
  ibv_destroy_cq(cq);
clean_pd:
  ibv_dealloc_pd(pd);
fail_create_pd:

  ibv_destroy_comp_channel(comp_channel);

fail_create_comp_channel:

  return tl::unexpected(result);
}
RdmaConnection::RdmaConnection(
    EventLoop *loop, const std::string &name, ibv_comp_channel *comp_channel,
    ibv_pd *pd, ibv_cq *cq, ibv_qp *qp, ibv_mr *mr,
    std::vector<uint8_t, memalign_allocator<uint8_t>> local_mem,
    uint32_t local_lkey, std::unique_ptr<DevContext> dev_ctx,
    std::unique_ptr<ConnDest> local_dest, std::unique_ptr<Channel> channel,
    int wc_capacity, size_t mem_num) noexcept
    : loop_(loop), name_(name), comp_channel_(comp_channel), pd_(pd), cq_(cq),
      qp_(qp), mr_(mr), local_mem_(std::move(local_mem)),
      local_lkey_(local_lkey), dev_ctx_(std::move(dev_ctx)),
      local_dest_(std::move(local_dest)), channel_(std::move(channel)),
      wc_capacity_(wc_capacity) {
  wcs_.resize(wc_capacity_);

  uint32_t mem_len = local_mem_.size() / (2 * mem_num);

  uint64_t send_header = reinterpret_cast<uint64_t>(local_mem_.data());
  uint64_t recv_header = send_header + mem_len;

  for (int i = 0; i < mem_num; ++i) {
    bufpairs_.emplace_back(BufPair{send_header, mem_len, recv_header, mem_len});
    free_bufpairs_.emplace_back(i);
    send_header += mem_len * 2;
    recv_header += mem_len * 2;
  }

  channel_->setReadCallback(
      [this](Timestamp recvTime) { handleRead(recvTime); });
  channel_->setWriteCallback([this]() { handleWrite(); });
  channel_->setErrorCallback([this]() { handleError(); });
  channel_->setCloseCallback([this]() { handleClose(); });
}

RdmaConnection::~RdmaConnection() {
  SPDLOG_DEBUG("Deconstruct RdmaConnection: name {}", name_);
  int res = 0;
  if (qp_ != nullptr) {
    res = ibv_destroy_qp(qp_);
    if (res != 0) {
      SPDLOG_ERROR("ib_destroy_qp error");
    }
  }
  if (cq_ != nullptr) {
    res = ibv_destroy_cq(cq_);
    if (res != 0) {
      SPDLOG_ERROR("ib_destroy_cq error");
    }
  }
  if (mr_ != nullptr) {
    res = ibv_dereg_mr(mr_);
    if (res != 0) {
      SPDLOG_ERROR("ib_dereg_mr error");
    }
  }
  if (pd_ != nullptr) {
    res = ibv_dealloc_pd(pd_);
    if (res != 0) {
      SPDLOG_ERROR("ibv_dealloc_pd error");
    }
  }
  if (comp_channel_ != nullptr) {
    res = ibv_destroy_comp_channel(comp_channel_);
    if (res != 0) {
      SPDLOG_ERROR("ibv_destroy_comp_channel error");
    }
  }
}

RDMAError RdmaConnection::connectQp(const RdmaExchangeInfo &remoteInfo) & {

  int errnum = 0;
  auto result = RDMAError::kSuccess;
  ibv_qp_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = IBV_MTU_1024;
  attr.dest_qp_num = remoteInfo.qpn_;
  attr.rq_psn = remoteInfo.psn_;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer = 12;
  attr.ah_attr.dlid = remoteInfo.lid_;
  attr.ah_attr.port_num = dev_ctx_->ib_dev_port_;

  if (remoteInfo.gid_.global.interface_id) {
    attr.ah_attr.is_global = 1;
    // Set attributes of the Global Routing Headers (GRH)
    // When using RoCE, GRH must be configured!
    attr.ah_attr.grh.hop_limit = 1;
    attr.ah_attr.grh.dgid = remoteInfo.gid_;
    attr.ah_attr.grh.sgid_index = local_dest_->gid_index_;
  }
  unsigned int index = 0;
  errnum = ibv_modify_qp(qp_, &attr,
                         IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                             IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                             IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
  if (errnum != 0) {
    SPDLOG_ERROR("Fail to modify QP to RTR: {}", strerror(errnum));
    result = RDMAError::kQpError;
    return result;
  }

  result = fillRq();
  if (result != RDMAError::kSuccess) {
    SPDLOG_ERROR("fillRq() error");
    return result;
  }

  attr.qp_state = IBV_QPS_RTS;
  // The minimum time that a QP waits for ACK/NACK from remote QP
  attr.timeout = 14;
  attr.retry_cnt = 7;
  attr.rnr_retry = 6;
  attr.sq_psn = local_dest_->psn_;
  attr.max_rd_atomic = 1;
  errnum = ibv_modify_qp(qp_, &attr,
                         IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                             IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                             IBV_QP_MAX_QP_RD_ATOMIC);
  if (errnum != 0) {
    SPDLOG_ERROR("Failed to modify QP to RTS: {}", strerror(errnum));
    result = RDMAError::kQpError;
    return result;
  }

  // remote memory info
  remote_mem_ = remoteInfo.recv_addr_;
  remote_len_ = remoteInfo.recv_len_;
  remote_rkey_ = remoteInfo.recv_rkey_;
  return result;
}
void RdmaConnection::send(const void *addr, uint32_t length, uint64_t wr_id) & {
  if (loop_->isInLoopThread()) {
    if (sendInLoop(addr, length, wr_id) != RDMAError::kSuccess) {
      SPDLOG_ERROR("RdmaConnection {} send error", name_);
    }
  } else {
    SPDLOG_WARN("RDMA Send not in LoopThread");
    // I don't know whether use shared_ptr or this.
    // auto ptr = shared_from_this();
    loop_->runInLoop([this, addr, length, wr_id] {
      if (this->sendInLoop(addr, length, wr_id) != RDMAError::kSuccess) {
        SPDLOG_ERROR("RdmaConnection {} send error", name_);
      }
    });
  }
}

RDMAError RdmaConnection::sendInLoop(const void *addr, uint32_t length,
                                     uint64_t wr_id) & {
  loop_->assertInLoopThread();

  if (state_ == State::kDisconnected) {
    SPDLOG_WARN("{} disconnected, give up send.", name_);
  }
  return postSend(reinterpret_cast<uint64_t>(addr), length, wr_id,
                  IBV_SEND_SIGNALED);
}

void RdmaConnection::connectEstablished() & {
  loop_->assertInLoopThread();
  assert(state_ == State::kConnecting);
  setState(State::kConnected);
  channel_->tie(shared_from_this());
  channel_->enableNoPriorityReading();
}

void RdmaConnection::connectDestroyed() & {
  loop_->assertInLoopThread();
  if (state_ == State::kConnected) {
    setState(State::kDisconnected);
    channel_->disableAll();
    disconnectedCallback_(shared_from_this());
  }
  channel_->remove();
}

void RdmaConnection::handleRead(Timestamp recvTime) {
  ibv_cq *ev_cq;
  void *ev_ctx;
  auto errnum = ibv_get_cq_event(comp_channel_, &ev_cq, &ev_ctx);
  assert(cq_ == ev_cq);
  if (errnum != 0) {
    SPDLOG_ERROR("ibv_get_cq_event error: {}", errnum);
    return;
  }
  if (++unack_cq_events_ == kUnackCqEventThreshold) {
    ibv_ack_cq_events(ev_cq, unack_cq_events_);
    unack_cq_events_ = 0;
  }

  errnum = ibv_req_notify_cq(ev_cq, 0);
  if (errnum != 0) {
    SPDLOG_ERROR("ibv_req_notify_cq error: {}", strerror(errnum));
    return;
  }

  int ne = 0;
  // bool is_first = true;
  do {
    ne = ibv_poll_cq(cq_, wc_capacity_, wcs_.data());
    if (ne < 0) {
      SPDLOG_ERROR("Fail to poll CQ {}", ne);
      std::terminate();
      return;
    }

    for (int i = 0; i < ne; ++i) {
      auto &wc = wcs_[i];
      // fail
      if (wc.status != IBV_WC_SUCCESS) {
        SPDLOG_ERROR("Work request status is {}", ibv_wc_status_str(wc.status));
        if (wc.opcode == IBV_WC_SEND) {
          sendCompleteFailCallback_(shared_from_this(), wc);
        } else if (wc.opcode == IBV_WC_RECV) {
          recvFailCallback_(shared_from_this(), wc);
        } else {
          SPDLOG_ERROR("other opcode fail: {}", int(wc.opcode));
        }
        std::terminate();
        continue;
      }
      // success
      if (wc.opcode == IBV_WC_SEND) {
        sendCompleteSuccessCallback_(shared_from_this(), wc);
      } else if (wc.opcode == IBV_WC_RECV) {
        auto bufpair_id = wc.wr_id;
        // SPDLOG_DEBUG("RecvComplete. bufpair_id {}", bufpair_id);
        if (postRecv(bufpair_id) != RDMAError::kSuccess) {
          SPDLOG_ERROR("RdmaConnection {} post recv error", name_);
        }
        recvSuccessCallback_(
            shared_from_this(),
            reinterpret_cast<uint8_t *>(bufpairs_[bufpair_id].recv_header),
            wc.byte_len, wc);
      } else {
        SPDLOG_ERROR("other opcode success: {}", int(wc.opcode));
      }
    }
  } while (ne > 0);
}

void RdmaConnection::handleWrite() {
  SPDLOG_ERROR("RdmaConnection should not have write event");
}

void RdmaConnection::handleClose() {
  SPDLOG_ERROR("Rdma Close handle not implement");
}

void RdmaConnection::handleError() {
  SPDLOG_ERROR("RdmaConnection {} . channel error", name_);
}

} // namespace rdma
} // namespace network
} // namespace hdc