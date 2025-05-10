#include <dirent.h>
#include <network/rdma/DevContext.h>
#include <spdlog/spdlog.h>
namespace hdc::network::rdma {
// In a list of IB devices (dev_list), given a IB device's name
// (ib_dev_name), the function returns its ID.
static inline int ib_dev_id_by_name(const char *ib_dev_name,
                                    struct ibv_device **dev_list,
                                    int num_devices) {
  for (int i = 0; i < num_devices; i++) {
    if (strcmp(ibv_get_device_name(dev_list[i]), ib_dev_name) == 0) {
      return i;
    }
  }

  return -1;
}

static int cmpfunc(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}
DevContext::DevContext() noexcept
    : ctx_(nullptr), ib_dev_name_(std::move(std::string(""))) {}

DevContext::DevContext(std::string ib_dev_name, int ib_dev_port,
                       ibv_context *ctx, uint64_t guid) noexcept
    : ib_dev_name_(std::move(ib_dev_name)), ib_dev_port_(ib_dev_port),
      ctx_(ctx), guid_(guid) {}

DevContext::DevContext(DevContext &&dev_ctx) noexcept {
  ib_dev_name_ = std::move(dev_ctx.ib_dev_name_);
  ib_dev_port_ = dev_ctx.ib_dev_port_;
  memcpy(gid_index_list_, dev_ctx.gid_index_list_, sizeof(gid_index_list_));
  memcpy(gid_list_, dev_ctx.gid_list_, sizeof(gid_list_));
  gid_count_ = dev_ctx.gid_count_;
  guid_ = dev_ctx.guid_;
  ctx_ = dev_ctx.ctx_;
  dev_attr_ = dev_ctx.dev_attr_;
  port_attr_ = dev_ctx.port_attr_;

  dev_ctx.ctx_ = nullptr;
}
DevContext::~DevContext() {
  if (ctx_ == nullptr) {
    return;
  }
  auto res = ibv_close_device(ctx_);
  if (res != 0) {
    SPDLOG_ERROR("ibv_close_device error");
  }
}

void DevContext::get_rocev2_gid_index() {
  const size_t max_gid_count =
      sizeof(gid_index_list_) / sizeof(gid_index_list_[0]);
  int gid_index_list[max_gid_count];
  int gid_count = 0;

  char dir_name[128] = {0};
  snprintf(dir_name, sizeof(dir_name),
           "/sys/class/infiniband/%s/ports/%d/gid_attrs/types",
           ib_dev_name_.data(), ib_dev_port_);
  DIR *dir = opendir(dir_name);
  if (!dir) {
    SPDLOG_ERROR("Fail to open folder {}", dir_name);
    return;
  }

  struct dirent *dp = NULL;
  char file_name[384] = {0};
  FILE *fp = NULL;
  ssize_t read;
  char *line = NULL;
  size_t len = 0;
  int gid_index;

  while ((dp = readdir(dir)) && gid_count < max_gid_count) {
    gid_index = atoi(dp->d_name);

    snprintf(file_name, sizeof(file_name), "%s/%s", dir_name, dp->d_name);
    fp = fopen(file_name, "r");
    if (!fp) {
      continue;
    }

    read = getline(&line, &len, fp);
    fclose(fp);
    if (read <= 0) {
      continue;
    }

    if (strncmp(line, "RoCE v2", strlen("RoCE v2")) != 0) {
      continue;
    }

    if (!is_ipv4_gid(gid_index)) {
      continue;
    }

    gid_index_list[gid_count++] = gid_index;
  }

  closedir(dir);

  qsort(gid_index_list, gid_count, sizeof(int), cmpfunc);
  gid_count_ = gid_count;
  for (int i = 0; i < gid_count; i++) {
    gid_index_list_[i] = gid_index_list[i];
  }
}

bool DevContext::is_ipv4_gid(int gid_index) {
  char file_name[384] = {0};
  static const char ipv4_gid_prefix[] = "0000:0000:0000:0000:0000:ffff:";
  FILE *fp = NULL;
  ssize_t read;
  char *line = NULL;
  size_t len = 0;
  snprintf(file_name, sizeof(file_name),
           "/sys/class/infiniband/%s/ports/%d/gids/%d", ib_dev_name_.data(),
           ib_dev_port_, gid_index);
  fp = fopen(file_name, "r");
  if (!fp) {
    return false;
  }
  read = getline(&line, &len, fp);
  fclose(fp);
  if (!read) {
    return false;
  }
  return strncmp(line, ipv4_gid_prefix, strlen(ipv4_gid_prefix)) == 0;
}

tl::expected<std::unique_ptr<DevContext>, RDMAError>
DevContext::create(std::string_view ib_dev_name, int ib_dev_port) {
  auto result = RDMAError::kSuccess;
  ibv_device **dev_list = nullptr;
  int ib_dev_id = -1;
  int num_devices;
  ibv_context *context = nullptr;
  uint64_t guid = 0;
  // Get IB device list
  dev_list = ibv_get_device_list(&num_devices);
  if (!dev_list) {
    SPDLOG_ERROR("Fail to get IB device list");
    result = RDMAError::kFindDeviceError;
    goto fail_get_device_list;
  } else if (num_devices == 0) {
    SPDLOG_ERROR("No IB devices found");
    result = RDMAError::kFindDeviceError;
    goto clean_dev_list;
  }

  ib_dev_id = ib_dev_id_by_name(ib_dev_name.data(), dev_list, num_devices);
  if (ib_dev_id < 0) {
    SPDLOG_ERROR("Fail to find IB device {}", ib_dev_name);
    result = RDMAError::kFindDeviceError;
    goto clean_dev_list;
  }

  context = ibv_open_device(dev_list[ib_dev_id]);
  if (context) {
    SPDLOG_TRACE("Open IB device {}", ibv_get_device_name(dev_list[ib_dev_id]));
  } else {
    SPDLOG_ERROR("Fail to open IB device {}",
                 ibv_get_device_name(dev_list[ib_dev_id]));
    result = RDMAError::kOpenDeviceError;
    goto clean_dev_list;
  }

  guid = ibv_get_device_guid(dev_list[ib_dev_id]);

  {
    auto dev_context = std::make_unique<DevContext>(
        std::move(std::string(ib_dev_name)), ib_dev_port, context, guid);
    dev_context->get_rocev2_gid_index();
    if (dev_context->gid_count_ == 0) {
      SPDLOG_ERROR("Cannot find any RoCE v2 GID");
      result = RDMAError::kDeviceInfoError;
      goto clean_dev_list;
    }
    // Get RoCE v2 GIDs
    for (size_t i = 0; i < dev_context->gid_count_; i++) {
      if (ibv_query_gid(dev_context->ctx_, dev_context->ib_dev_port_,
                        dev_context->gid_index_list_[i],
                        &(dev_context->gid_list_[i])) != 0) {
        SPDLOG_ERROR("Cannot read GID of index {}",
                     dev_context->gid_index_list_[i]);
        result = RDMAError::kDeviceInfoError;
        goto clean_dev_list;
      }
    }
    ibv_free_device_list(dev_list);
    return dev_context;
  }

clean_dev_list:
  ibv_free_device_list(dev_list);
fail_get_device_list:
  return tl::unexpected(result);
}
} // namespace hdc::network::rdma