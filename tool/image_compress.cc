#include "doca/common.h"
#include "doca/compress.h"
#include "doca/params.h"
#include "doca_compress.h"
#include "doca_ctx.h"
#include "doca_error.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <gflags/gflags.h>
#include <ios>
#include <memory>
#include <network/EventLoop.h>
#include <optional>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <thread>

using hdc::network::EventLoop;
namespace fs = std::filesystem;


DEFINE_string(input_folder, "data/uncompress",
              "The input folder of uncompress layers");
DEFINE_string(output_folder, "data/registry/content_layers_64m",
              "The output folder of segmented and compressed layers");
DEFINE_string(doca_pci_address, "31:00.0", "The PCI address of DPU engine");
DEFINE_uint64(segment_size, (1ULL << 26),
              "size to split the compressed layer into");

class ImageCompress {

private:
  CompressEngine engine_;
  fs::directory_iterator input_it_;
  std::string output_folder_;
  std::ifstream input_file_;
  size_t input_size_;
  uint64_t job_id_{0};
  size_t idx_{0};
  size_t head_{0};
  EventLoop *loop_;
  const size_t segment_size_;

public:
  ImageCompress(CompressEngine engine, fs::directory_iterator input_it,
                std::string output_folder, EventLoop *loop, size_t segment_size)
      : engine_(std::move(engine)), input_it_(std::move(input_it)),
        output_folder_(std::move(output_folder)), loop_(loop),
        segment_size_(segment_size) {
    engine_.setCompressSuccessCallback(
        [this](CompressEngine &engine, uint64_t job_id, uint8_t *src_addr,
               size_t src_len, uint8_t *dst_addr, size_t dst_len) {
          this->onTaskSuccess(engine, job_id, src_addr, src_len, dst_addr,
                              dst_len);
        });
    engine_.setCompressErrorCallback(
        [this](CompressEngine &engine, doca_error_t err) {
          this->onTaskError(engine, err);
        });
    fs::directory_iterator end_iter;

    while (input_it_ != end_iter && !input_it_->is_regular_file()) {
      input_it_++;
    }
    if (input_it_ == end_iter) {
      return;
    }
    input_file_ = std::ifstream{input_it_->path(), std::ios::binary};
    if (!input_file_.is_open()) {
      SPDLOG_ERROR("Fail to open input file: {}", input_it_->path().string());
      return;
    }
    input_file_.seekg(0, std::ios::end);
    input_size_ = static_cast<size_t>(input_file_.tellg());
    input_file_.seekg(0, std::ios::beg);
    auto read_size = std::min(input_size_ - head_, segment_size_);

    input_file_.read(
        reinterpret_cast<char *>(engine_.get_bufpair(0).src_mem.data()),
        read_size);

    if (engine_.get_bufpair(0).src_doca_buf.set_data_by_offset(0, read_size) !=
        DOCA_SUCCESS) {
      SPDLOG_ERROR("set doca buf error");
      return;
    }
    engine_.start_job(job_id_++, DOCA_COMPRESS_DEFLATE_JOB, 0);
    SPDLOG_INFO("enque job {}", job_id_ - 1);
    head_ += read_size;
    idx_ += 1;
  }

  void loop() { loop_->loop(); }

  void start() { engine_.start(); }

  void onTaskSuccess(CompressEngine &engine, uint64_t job_id, uint8_t *src_addr,
                     size_t src_len, uint8_t *dst_addr, size_t dst_len) {
    SPDLOG_INFO("job {} complete", job_id);
    // create out file and write data.

    auto out_layer_path =
        output_folder_ + "/" + input_it_->path().filename().string() + "/";
    if (!fs::exists(out_layer_path)) {
      if (!fs::create_directory(out_layer_path)) {
        SPDLOG_ERROR("create directory: {} error", out_layer_path);
        return;
      }
    }
    auto out_file_path = out_layer_path + std::to_string(idx_ - 1) + ".tar.gz";
    std::ofstream out_file(out_file_path, std::ios::binary);
    if (out_file.is_open()) {
      out_file.write(reinterpret_cast<const char *>(dst_addr), dst_len);
      out_file.close();
    } else {
      SPDLOG_ERROR("Fail to open file {}", out_file_path);
      return;
    }

    // if need to read a new file
    if (head_ >= input_size_) {
      input_file_.close();
      std::ofstream total_segments_file(out_layer_path + "total_segment.txt");
      if (!total_segments_file.is_open()) {
        SPDLOG_ERROR("open file {} error",
                     out_layer_path + "total_segment.txt");
        return;
      }
      total_segments_file << idx_ << std::endl;
      total_segments_file.close();
      fs::directory_iterator end_iter;
      input_it_++;
      while (input_it_ != end_iter && !input_it_->is_regular_file()) {
        input_it_++;
      }
      if (input_it_ == end_iter) {
        return;
      }
      SPDLOG_INFO("new file {}", input_it_->path().string());

      input_file_ = std::ifstream{input_it_->path(), std::ios::binary};
      if (!input_file_.is_open()) {
        SPDLOG_ERROR("Fail to open input file: {}", input_it_->path().string());
        return;
      }
      input_file_.seekg(0, std::ios::end);
      input_size_ = static_cast<size_t>(input_file_.tellg());
      input_file_.seekg(0, std::ios::beg);
      head_ = 0;
      idx_ = 0;
    }

    auto read_size = std::min(input_size_ - head_, segment_size_);
    input_file_.read(
        reinterpret_cast<char *>(engine_.get_bufpair(0).src_mem.data()),
        read_size);
    if (engine_.get_bufpair(0).src_doca_buf.set_data_by_offset(0, read_size) !=
        DOCA_SUCCESS) {
      SPDLOG_ERROR("set doca buf error");
      return;
    }
    engine.start_job(job_id_++, DOCA_COMPRESS_DEFLATE_JOB, 0);
    SPDLOG_INFO("enque job {}", job_id_ - 1);
    head_ += read_size;
    idx_ += 1;
  }

  void onTaskError(CompressEngine &engine, doca_error_t err) {
    SPDLOG_ERROR("Task error: {}", doca_get_error_string(err));
  }
};

int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::trace);
  spdlog::set_pattern("%^[%L][%T.%e]%$[%s:%#] %v");
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

  EventLoop loop;

  auto engine = CompressEngine::create(FLAGS_doca_pci_address.data(),
                                       DOCA_BUF_EXTENSION_NONE, 16, &loop,
                                       MAX_FILE_SIZE, MAX_FILE_SIZE, 1);
  if (!engine.has_value()) {
    SPDLOG_ERROR("create engine error");
    return EXIT_FAILURE;
  }

  if (engine->src_mmaps_start(std::nullopt) != DOCA_SUCCESS) {
    SPDLOG_ERROR("src buf mmap error");
    return -1;
  }
  if (engine->dst_mmaps_start(std::nullopt) != DOCA_SUCCESS) {
    SPDLOG_ERROR("dst buf mmap error");
    return -1;
  }
  if (!fs::exists(FLAGS_input_folder)) {
    SPDLOG_ERROR("input_folder:{} not exist", FLAGS_input_folder);
    return -1;
  }

  if (!fs::exists(FLAGS_output_folder)) {
    if (!fs::create_directory(FLAGS_output_folder)) {
      SPDLOG_ERROR("create directory: {} error", FLAGS_output_folder);
      return -1;
    }
  }


  ImageCompress image_compress{std::move(*engine),
                               fs::directory_iterator(FLAGS_input_folder),
                               FLAGS_output_folder, &loop, FLAGS_segment_size};

  image_compress.start();
  image_compress.loop();
  return 0;
}
