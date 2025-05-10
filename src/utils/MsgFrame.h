#pragma once
#include "network/tcp/Callbacks.h"
#include <cassert>
#include <cstdint>
#include <netinet/in.h>
#include <network/tcp/Buffer.h>
#include <network/tcp/TcpConnection.h>
#include <spdlog/spdlog.h>
using hdc::network::tcp::Buffer;
using hdc::network::tcp::TcpConnectionPtr;
const int kFrameHeaderLen = sizeof(int);

template <typename T> void sendTcpPbMsg(const TcpConnectionPtr &conn, T &&msg) {
  auto len = htonl(static_cast<int>(msg.ByteSizeLong()));
  conn->send(&len, 4);
  conn->send(msg.SerializeAsString());
}

template <typename T> bool receiveTcpPbMsg(Buffer *buffer, T &msg) {
  if (buffer->readableBytes() < kFrameHeaderLen) {
    return false;
  }
  if (buffer->readableBytes() < kFrameHeaderLen + buffer->peekInt32()) {
    return false;
  }
  auto len = buffer->readInt32();
  if (msg.ParseFromArray(buffer->peek(), len) == false) {
    SPDLOG_ERROR("parse msg error");
    buffer->retrieve(len);
    return false;
  }
  buffer->retrieve(len);
  return true;
}

template <typename T>
void sendTcpPbMsg(const TcpConnectionPtr &conn, T &&msg, int msg_type) {
  uint8_t buf[8];
  *reinterpret_cast<uint32_t *>(buf) = htonl(msg_type);
  *reinterpret_cast<int *>(buf + 4) =
      htonl(static_cast<int>(msg.ByteSizeLong()));
  conn->send(buf, 8);
  conn->send(msg.SerializeAsString());
}

template <typename T>
bool receiveTcpPbMsg(Buffer *buffer, T &msg, int msg_type) {
  if (buffer->readableBytes() < 8) {
    return false;
  }
  auto p = buffer->peek();
  auto type = ntohl(*reinterpret_cast<const int *>(p));
  auto len = ntohl(*reinterpret_cast<const int *>(p + 4));
  assert(msg_type == type);
  if (buffer->readableBytes() < 8 + len) {
    return false;
  }
  buffer->retrieve(8);
  if (msg.ParseFromArray(buffer->peek(), len) == false) {
    SPDLOG_ERROR("parse msg error");
    buffer->retrieve(len);
    return false;
  }
  buffer->retrieve(len);
  return true;
}

template <typename T>
int parseRdmaPbMsg(uint8_t *recv_buf, uint32_t recv_len, T &msg) {
  if (recv_len < kFrameHeaderLen) {
    SPDLOG_ERROR("Recv len {} < {}", recv_len, kFrameHeaderLen);
    return -1;
  }
  auto msg_len = *reinterpret_cast<int *>(recv_buf);
  if (recv_len < msg_len + kFrameHeaderLen) {
    SPDLOG_ERROR("Frame incomplete. Recv len {} < {}", recv_len,
                 msg_len + kFrameHeaderLen);
    return -1;
  }
  if (msg.ParseFromArray(recv_buf + kFrameHeaderLen, msg_len) == false) {
    SPDLOG_ERROR("prase msg error");
    return -1;
  }
  return msg_len + kFrameHeaderLen;
}

template <typename T>
int serializeRdmaPbMsg(uint8_t *send_buf, uint32_t send_cap, T &msg) {
  auto msg_len = static_cast<int>(msg.ByteSizeLong());
  if (send_cap < kFrameHeaderLen + msg_len) {
    SPDLOG_ERROR("send_cap {} < frame len", send_cap);
    return -1;
  }
  *reinterpret_cast<int *>(send_buf) = msg_len;
  if (msg.SerializeToArray(send_buf + kFrameHeaderLen, msg_len) == false) {
    SPDLOG_ERROR("serial msg error");
    return -1;
  }
  return msg_len + kFrameHeaderLen;
}
