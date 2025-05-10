#pragma once
#include <algorithm>
#include <bits/types/FILE.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/types.h>
#include <host/client/metadata.h>
#include <host/client/offload_client_epoll.h>

namespace hdc {
namespace host {
namespace client {


using UntarResultQueue = folly::UMPSCQueue<UntarResult, false>;
using UntarResultQueuePtr = std::shared_ptr<UntarResultQueue>;
using UntarDataQueue = folly::USPSCQueue<UntarData, false>;
using UntarDataQueuePtr = std::shared_ptr<UntarDataQueue>;

class UntarEngine {

private:

  static const std::string untar_command_prefix;
  folly::ConcurrentHashMap<std::string, UntarDataQueuePtr> untar_map_;
  folly::CPUThreadPoolExecutor executor_;
  // UntarResultQueuePtr result_producer_;
  OffloadClientEpoll* offload_client_;
  const std::string untar_file_path_;


  static void untar_task(
      UntarDataQueuePtr task_queue, OffloadClientEpoll* offload_client,
      std::string cmd, std::string layer, std::string image_name_tag,
      folly::ConcurrentHashMap<std::string, UntarDataQueuePtr> &untar_map);

public:
  UntarEngine(size_t numThreads, OffloadClientEpoll* offload_client,
              std::string untar_file_path);

  void untar(UntarData data);
};
} // namespace client
} // namespace host
} // namespace hdc
