
#include "folly/concurrency/ConcurrentHashMap.h"
#include <chrono>
#include <host/client/untar_engine.h>
#include <spdlog/spdlog.h>
#include <string>
namespace hdc::host::client {

const std::string UntarEngine::untar_command_prefix = "tar xf - -C ";

void UntarEngine::untar_task(
    UntarDataQueuePtr task_queue, OffloadClientEpoll *offload_client,
    std::string cmd, std::string layer, std::string image_name_tag,
    folly::ConcurrentHashMap<std::string, UntarDataQueuePtr> &untar_map) {
  auto start_time = std::chrono::high_resolution_clock::now();
  FILE *fd = popen(cmd.c_str(), "w");
  if (!fd) {
    SPDLOG_ERROR("Error executing tar command, layer {}.", layer);
    offload_client->completeTask(
        UntarResult{std::move(layer), std::move(image_name_tag), false});
    return;
  }
  SPDLOG_INFO("Untar task start. cmd: {}", cmd);
  while (true) {
    auto data = task_queue->dequeue();
    auto res = fwrite(data.segment_.data(), 1, data.segment_.size(), fd);
    if (res == 0) {
      SPDLOG_ERROR("Untar error: image {}, index {}, total {}", data.layer_,
                   data.index_, data.total_segments_);
      pclose(fd);
      offload_client->completeTask(
          UntarResult{std::move(layer), std::move(image_name_tag), false});
      return;
    }
    if (data.index_ == (data.total_segments_ - 1)) {
      break;
    }
  }
  auto status = pclose(fd);
  untar_map.erase(layer);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration<double, std::milli>(end_time - start_time).count();
  SPDLOG_INFO("Image: {}, untar_rtt {}ms", image_name_tag, duration);

  if (status != 0) {
    SPDLOG_ERROR("Untar task close error. layer {}. status {}", layer, status);
    offload_client->completeTask(
        UntarResult{std::move(layer), std::move(image_name_tag), false});
  } else {
    SPDLOG_INFO("Untar task finish: layer {}", layer);
    offload_client->completeTask(
        UntarResult{std::move(layer), std::move(image_name_tag), true});
  }
  return;
}

UntarEngine::UntarEngine(size_t numThreads, OffloadClientEpoll *offload_client,
                         std::string untar_file_path)
    : untar_map_(), executor_(numThreads), offload_client_(offload_client),
      untar_file_path_(std::move(untar_file_path)) {}

void UntarEngine::untar(UntarData data) {
  const auto layer = data.layer_;
  const auto &image = data.image_name_tag_;
  auto it = untar_map_.find(layer);
  if (it == untar_map_.end()) {
    // create file for image
    auto file_path = untar_file_path_ + "/" + image + "/" + layer;
    if (!std::filesystem::exists(file_path)) {
      std::filesystem::create_directories(file_path);
      // SPDLOG_INFO("Crete directory {}", file_path);
    } else {
      SPDLOG_ERROR("Create directory {} error", file_path);
    }
    // create task
    auto cmd = untar_command_prefix + file_path;
    auto task_queue = std::make_shared<UntarDataQueue>();

    executor_.add([task_queue, cmd = std::move(cmd), layer = layer,
                   image = data.image_name_tag_, this]() {
      untar_task(std::move(task_queue), this->offload_client_, std::move(cmd),
                 std::move(layer), std::move(image), untar_map_);
    });
    task_queue->enqueue(std::move(data));
    untar_map_.emplace(std::move(layer), std::move(task_queue));
  } else {
    it->second->enqueue(std::move(data));
  }
  return;
}

} // namespace hdc::host::client