#pragma once
#include "doca/compress.h"
#include "doca_error.h"
#include "dpu/metadata.h"
#include "network/rdma/RdmaClient.h"
#include "network/rdma/RdmaConfig.h"
#include "network/rdma/RdmaConnection.h"
#include "utils/blob_pool.h"
#include <chrono>
#include <compress.pb.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace hdc {
namespace dpu {
using hdc::network::EventLoop;
using hdc::network::InetAddress;
using hdc::network::rdma::RdmaClient;
using hdc::network::rdma::RdmaConfig;
using hdc::network::rdma::RdmaConnection;
using hdc::network::rdma::RdmaConnectionPtr;

class DecompressClientEpoll {

public:
  DecompressClientEpoll(CompressEngine compress_engine,
                        //  DmaEngine dma_engine,
                        EventLoop *loop, const InetAddress &listen_addr,
                        RdmaConfig rdma_config,
                        std::shared_ptr<BlobPool> blob_pool);

  /// @brief: RDMA connect. Thread safe.
  void connect();

  /// @brief: enable epoll for DOCA. Not thread safe.
  void decompressStart();

  /// @brief: submit a decompress task to client. Thread safe.
  void submitDecompressTask(ContentElement content);

  std::shared_ptr<BlobPool> get_blob_pool() { return blob_pool_; }

private:
  enum class MsgType : int {
    kMmapInfo,
    kDecompressFinish,
  };
  struct JobInfo {
    std::string image_name_tag;
    std::string layer;
    int bufpair_id;
    int total_segments;
    int segment_idx;
  };

  struct RdmaInfo {
    std::string image_name_tag;
    std::string layer;
    int total_segments;
    int segment_idx;
    size_t dst_len;
    int mmap_id;
    bool data_inline;
    // std::vector<uint8_t> segment{vector<uint8_t>(0)};
    Blob segment;
  };

  CompressEngine compress_engine_;
  // DmaEngine dma_engine_;
  RdmaClient client_;
  EventLoop *loop_;
  std::shared_ptr<BlobPool> blob_pool_;

  std::deque<ContentElement> pending_compress_jobs_;
  std::deque<ContentElement> pending_dma_jobs_;
  std::deque<RdmaInfo> pending_rdma_jobs_;
  // record the bufpair_id of inflight_sends_;
  std::deque<size_t> inflight_sends_;
  bool decompress_busy_{false};
  bool dma_busy_{false};
  bool connected_{false};
  RdmaConnectionPtr conn_{nullptr};
  uint64_t wr_id_{0};
  uint64_t job_id_{0};
  // record on-going job
  JobInfo compress_job_;
  JobInfo dma_job_;
  
  // record the image translate time without pipeline.
  std::chrono::high_resolution_clock::time_point start_time_;
  double decompress_duration_{0};
  std::string curr_image_tag_{""};

  bool handleMmapInfoResponse(const compress::MmapInfoResponse &resp);

  bool handleDecompressFinishResponse(
      const compress::DecompressFinishResponse &resp);

  bool tryStartDecompressJob();

  bool startDecompressJob(ContentElement &task, size_t bufpair_id);

  // bool tryStartDmaJob();

  void onConnected(const RdmaConnectionPtr &conn);

  void onRecvSuccess(const RdmaConnectionPtr &conn, uint8_t *recv_buf,
                     uint32_t recv_len, const ibv_wc &wc);

  void onRecvFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteSuccess(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onSendCompleteFail(const RdmaConnectionPtr &conn, const ibv_wc &wc);

  void onDecompressTaskSuccess(CompressEngine &engine, uint64_t job_id,
                               uint8_t *src_addr, size_t src_len,
                               uint8_t *dst_addr, size_t dst_len);

  void onDecompressTaskError(CompressEngine &engine, doca_error_t err);

  void sendDecompressFinishRequest(RdmaInfo info,
                                   const RdmaConnection::SendBuf &free_buf);
};
} // namespace dpu
} // namespace hdc