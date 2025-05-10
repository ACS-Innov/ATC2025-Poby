#include "network/rdma/Callbacks.h"
#include "network/rdma/RdmaConfig.h"
#include "network/rdma/RdmaExchangeInfo.h"
#include "network/rdma/error.h"
#include "network/tcp/Buffer.h"
#include "network/tcp/Callbacks.h"
#include "network/tcp/TcpClient.h"
#include <cassert>
#include <cstdint>
#include <infiniband/verbs.h>
#include <memory>
#include <network/rdma/RdmaConnection.h>
#include <spdlog/spdlog.h>

using hdc::network::tcp::TcpConnectionPtr;
namespace hdc::network::rdma {

RdmaConnector::RdmaConnector() : state_(State::kRdmaNotInit) {}

void RdmaConnector::initialRdmaResource(const RdmaConfig &config,
                                        const TcpConnectionPtr &tcpConn) {

  auto conn_name = "Rdma_" + tcpConn->localAddress().toIpPort() + "<->" +
                   tcpConn->peerAddress().toIpPort();
  auto conn = RdmaConnection::create(config.ibDevName_, config.ibDevPort_,
                                     config.memSize_, config.memNum_,
                                     tcpConn->getLoop(), conn_name);
  if (!conn.has_value()) {
    SPDLOG_ERROR("Create RdmaConnection error");
    tcpConn->shutdown();
    return;
  }
  connection_ = *conn;
  setLocalExchangeInfo();
  tcpConn->send(&localExchangeInfo_, sizeof(localExchangeInfo_));
  state_ = State::kRdmaInit;
  return;
}

void RdmaConnector::setLocalExchangeInfo() {
  localExchangeInfo_.gid_ = connection_->local_dest_->gid_;
  localExchangeInfo_.lid_ = connection_->local_dest_->lid_;
  localExchangeInfo_.psn_ = connection_->local_dest_->psn_;
  localExchangeInfo_.qpn_ = connection_->qp_->qp_num;
  localExchangeInfo_.recv_addr_ = connection_->bufpairs_[0].recv_header;
  localExchangeInfo_.recv_len_ = connection_->bufpairs_[0].recv_cap;
  localExchangeInfo_.recv_rkey_ = connection_->mr_->rkey;
}

const RdmaExchangeInfo &RdmaConnector::getLocalExchangeInfo() const {
  return localExchangeInfo_;
}

void RdmaConnector::handshakeQp(const TcpConnectionPtr &conn, Buffer *buffer) {
  RdmaExchangeInfo remoteExchangeInfo;
  if (state_ == State::kRdmaInit) {
    if (buffer->readableBytes() < sizeof(remoteExchangeInfo)) {
      return;
    }

    auto ptr = buffer->peek();
    memcpy(&remoteExchangeInfo, ptr, sizeof(remoteExchangeInfo));
    buffer->retrieve(sizeof(remoteExchangeInfo));

    if (connection_->connectQp(remoteExchangeInfo) != RDMAError::kSuccess) {
      SPDLOG_ERROR("RdmaConnection connectQp() error");
      conn->shutdown();
      return;
    }

    state_ = State::kRdmaRTS;
    newRdmaConnectionCallback_(connection_);
    conn->send(&kHandshakeCompleteMsg, sizeof(kHandshakeCompleteMsg));
  }
  if (state_ == State::kRdmaRTS) {
    if (buffer->readableBytes() < sizeof(kHandshakeCompleteMsg)) {
      return;
    }
    assert(buffer->peekInt8() == kHandshakeCompleteMsg);

    buffer->retrieveInt8();
    state_ = State::kRdmaConnected;
    connection_->callConnectedCallback();
    connection_ = nullptr;
    conn->shutdown();
  }
}

} // namespace hdc::network::rdma