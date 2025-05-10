#pragma once
#include <cstdint>
#include <infiniband/verbs.h>
struct RdmaExchangeInfo {
  // Local identifier
  uint16_t lid_;
  // Queue pair number
  uint32_t qpn_;
  // Packet sequence number
  uint32_t psn_;
  union ibv_gid gid_;
  // memory
  uint64_t recv_addr_;
  uint32_t recv_len_;
  uint32_t recv_rkey_;
} __attribute__((packed));