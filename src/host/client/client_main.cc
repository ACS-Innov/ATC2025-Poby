#include "doca/common.h"
#include <spdlog/common.h>
#include <thread>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/rdma/RdmaConfig.h"
#include <gflags/gflags.h>
#include <host/client/command_server.h>
#include <host/client/decompress_server_epoll.h>
#include <host/client/offload_client_epoll.h>
#include <spdlog/spdlog.h>
using hdc::host::client::CommandServer;
using hdc::host::client::DecompressServerEpoll;
using hdc::host::client::OffloadClientEpoll;
using hdc::host::client::OffloadTaskQueue;
using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaConfig;
// decompress server
DEFINE_string(decompress_server_ib_dev_name, "mlx5_0",
              "The IB device name of decompress engine in host");
DEFINE_int32(decompress_server_ib_dev_port, 1,
             "The IB device port of decompress engine in host");
DEFINE_string(decompress_server_listen_ip, "172.24.46.186",
              "The IP address of host listening for decompress engine");
DEFINE_int32(decompress_server_listen_port, 9001,
             "The IP port of host listening for decompress engine");
DEFINE_uint32(
    decompress_server_rdma_mem, 4096,
    "The memory of RDMA sendbuf/recvbuf for decompress engine in bytes");
DEFINE_uint64(decompress_server_rdma_mem_num, 4,
              "The number of RDMA buf for decompress engine");
DEFINE_string(decompress_server_pci_address, "31:00.0",
              "The PCI address of host decompress engine");
DEFINE_int32(decompress_server_doca_workq_depth, 16,
             "DOCA workq depth of decompress engine");
DEFINE_int32(decompress_server_doca_mem_num, 4,
             "DOCA memory num of decompress engine");
DEFINE_uint64(decompress_server_doca_mem, 128 * 1024 * 1024,
              "DOCA memory in bytes");
DEFINE_uint64(decompress_server_untar_num_threads, 3,
              "The number of threads for untar");
DEFINE_string(decompress_server_untar_file_path, "untar/design",
              "File path prefix to store untar layers");
// offload client
DEFINE_string(offload_client_ib_dev_name, "mlx5_0",
              "The IB device name of offload client");
DEFINE_int32(offload_client_ib_dev_port, 1,
             "The IB device port of offload client");
DEFINE_string(offload_client_peer_ip, "172.24.46.86",
              "The IP address of host listening for offload client");
DEFINE_uint32(offload_client_peer_port, 9003,
              "The IP port of host listening for offload client");
DEFINE_uint32(offload_client_rdma_mem, 4096,
              "The memory of RDMA sendbuf/recvbuf for offload client in bytes");
DEFINE_uint64(offload_client_rdma_mem_num, 4,
              "The number of RDMA buf for offload client");
DEFINE_string(offload_client_metadata_path, "data/metadata",
              "image metadata path for offload client");
// command server
DEFINE_string(command_server_ip, "0.0.0.0",
              "The ip address of command server for listening.");
DEFINE_int32(command_server_port, 9000,
             "The port of command server for listening");
DEFINE_int64(command_server_thread_num, 0,
             "The number of I/O thread for handling TCP connection (TCP listen "
             "is in another separate thread).");
int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("%^[%L][%T.%e]%$[%s:%#] %v");
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

  auto loop = EventLoop();

  // offload client

  RdmaConfig offload_client_rdma_config = {
      FLAGS_offload_client_ib_dev_name, FLAGS_offload_client_ib_dev_port,
      FLAGS_offload_client_rdma_mem, FLAGS_offload_client_rdma_mem_num};
  OffloadClientEpoll offload_client{
      &loop,
      InetAddress{FLAGS_offload_client_peer_ip,
                  static_cast<uint16_t>(FLAGS_offload_client_peer_port)},
      std::move(offload_client_rdma_config),
      FLAGS_offload_client_metadata_path};

  // decompress server
  RdmaConfig decompress_server_rdma_config = {
      FLAGS_decompress_server_ib_dev_name, FLAGS_decompress_server_ib_dev_port,
      FLAGS_decompress_server_rdma_mem, FLAGS_decompress_server_rdma_mem_num};
  auto compress_engine = CompressEngine::create(
      FLAGS_decompress_server_pci_address.data(), DOCA_BUF_EXTENSION_NONE,
      FLAGS_decompress_server_doca_workq_depth, &loop, 8, FLAGS_decompress_server_doca_mem,
      FLAGS_decompress_server_doca_mem_num);

  if (!compress_engine.has_value()) {
    SPDLOG_ERROR("create engine error");
    return 0;
  }

  DecompressServerEpoll decompress_server{
      std::move(*compress_engine),
      //   std::move(*dma_engine),
      &loop,
      InetAddress{FLAGS_decompress_server_listen_ip,
                  static_cast<uint16_t>(FLAGS_decompress_server_listen_port)},
      std::move(decompress_server_rdma_config),
      FLAGS_decompress_server_untar_num_threads,
      FLAGS_decompress_server_untar_file_path, &offload_client};

  // command server

  auto command_server_listen_addr =
      InetAddress(FLAGS_command_server_ip, FLAGS_command_server_port);

  auto command_server =
      CommandServer(&loop, command_server_listen_addr,
                    FLAGS_command_server_thread_num, &offload_client);
  command_server.start();
  decompress_server.start();
  offload_client.connect();
  loop.loop();
}