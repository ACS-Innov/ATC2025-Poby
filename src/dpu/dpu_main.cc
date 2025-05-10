#include "doca/common.h"
#include "doca_buf.h"
#include "dpu/decompress_client_epoll.h"
#include "dpu/offload_server_epoll.h"
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/rdma/RdmaConfig.h"
#include "utils/blob_pool.h"
#include <dpu/content_fetcher.h>
#include <future>
#include <gflags/gflags.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <thread>

using hdc::dpu::ContentFetcher;
using hdc::dpu::ContentFetcherPtr;
using hdc::dpu::ContentTaskQueue;
using hdc::dpu::ContentTaskQueuePtr;
using hdc::dpu::DecompressClientEpoll;
using hdc::dpu::OffloadServerEpoll;
using hdc::network::InetAddress;

using hdc::network::rdma::RdmaConfig;
DEFINE_string(offload_server_ib_dev_name, "mlx5_2",
              "The IB device name of offload server");
DEFINE_int32(offload_server_ib_dev_port, 1,
             "The IB device port of offload server");
DEFINE_string(offload_server_listen_ip, "172.24.46.86",
              "The IP address of host listening for offload server");
DEFINE_uint32(offload_server_listen_port, 9003,
              "The IP port of host listening for offload server");
DEFINE_uint32(offload_server_rdma_mem, 4096,
              "The memory of RDMA sendbuf/recvbuf for offload server in bytes");
DEFINE_uint64(offload_server_rdma_mem_num, 4,
              "The number of RDMA buf for offload server");
DEFINE_string(decompress_client_ib_dev_name, "mlx5_2",
              "The IB device name of decompress engine in DPU");
DEFINE_int32(decompress_client_ib_dev_port, 1,
             "The IB device port of decompress engine in DPU");
DEFINE_string(decompress_client_peer_ip, "172.24.46.186",
              "The IP address for decompress engine to connect");
DEFINE_int32(decompress_client_peer_port, 9001,
             "The IP port for decompress engine to connect");
DEFINE_uint32(
    decompress_client_rdma_mem, 4096,
    "The memory of RDMA sendbuf/recvbuf for decompress engine in bytes");
DEFINE_uint64(decompress_client_rdma_mem_num, 4,
              "The number of RDMA buf for decompress engine");
DEFINE_string(decompress_client_pci_address, "03:00.0",
              "The PCI address of DPU decompress engine");
DEFINE_int32(decompress_client_doca_workq_depth, 16,
             "DOCA workq depth of decompress engine");
DEFINE_int32(decompress_client_doca_mem_num, 4,
             "DOCA memory num of decompress engine");
DEFINE_uint64(decompress_client_doca_mem, 128 * 1024 * 1024,
              "DOCA memory in bytes");
DEFINE_int32(blob_num, 16, "the number of 128MB blob");
DEFINE_uint64(blob_size, 128 * 1024 * 1024, "the size of each blob");
void runDecompressClient(std::promise<DecompressClientEpoll *> p) {
  EventLoop loop;
  // decompress client
  RdmaConfig decompress_client_config{
      FLAGS_decompress_client_ib_dev_name, FLAGS_decompress_client_ib_dev_port,
      FLAGS_decompress_client_rdma_mem, FLAGS_decompress_client_rdma_mem_num};

  auto compress_engine = CompressEngine::create(
      FLAGS_decompress_client_pci_address.data(), DOCA_BUF_EXTENSION_NONE,
      FLAGS_decompress_client_doca_workq_depth, &loop,
      FLAGS_decompress_client_doca_mem, 8,
      FLAGS_decompress_client_doca_mem_num);
  if (!compress_engine.has_value()) {
    SPDLOG_ERROR("create compress_engine error");
    return;
  }

  auto blob_pool = std::make_shared<BlobPool>(FLAGS_blob_size, FLAGS_blob_num);
  DecompressClientEpoll decompress_client{
      std::move(*compress_engine),
      &loop,
      InetAddress{FLAGS_decompress_client_peer_ip,
                  static_cast<uint16_t>(FLAGS_decompress_client_peer_port)},
      std::move(decompress_client_config), blob_pool};
  p.set_value(&decompress_client);
  decompress_client.decompressStart();
  loop.loop();
}

void runContentFetcher(DecompressClientEpoll *decompress_client,
                       std::promise<std::shared_ptr<ContentFetcher>> p) {
  EventLoop loop;
  auto fetcher = std::make_shared<ContentFetcher>(&loop, decompress_client);
  p.set_value(fetcher);
  loop.loop();
}

int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("%^[%L][%T.%e]%$[%s:%#] %v");
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
  // decompress client
  std::promise<DecompressClientEpoll *> p;
  auto f = p.get_future();
  std::thread(runDecompressClient, std::move(p)).detach();
  auto decompress_client = f.get();

  // content fetcher
  std::promise<std::shared_ptr<ContentFetcher>> p1;
  auto f1 = p1.get_future();
  std::thread(runContentFetcher, decompress_client, std::move(p1)).detach();
  auto fetcher = f1.get();

  // offload server
  EventLoop loop;

  RdmaConfig offload_server_config{
      FLAGS_offload_server_ib_dev_name, FLAGS_offload_server_ib_dev_port,
      FLAGS_offload_server_rdma_mem, FLAGS_offload_server_rdma_mem_num};

  OffloadServerEpoll offload_server{
      &loop,
      InetAddress{FLAGS_offload_server_listen_ip,
                  static_cast<uint16_t>(FLAGS_offload_server_listen_port)},
      std::move(offload_server_config), fetcher, decompress_client};

  offload_server.start();
  loop.loop();
}
