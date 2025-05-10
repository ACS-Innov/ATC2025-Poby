#pragma once
#include "image/image_ops.h"
#include "isula_libutils/oci_image_spec.h"
#include "isula_libutils/registry_manifest_schema2.h"
#include "network/tcp/Callbacks.h"
#include <cstddef>
#include <fstream>
#include <iostream>
#include <network/tcp/TcpConnection.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
class ImageNameTag {
public:
  ImageNameTag() = default;

  ImageNameTag(const std::string &name, const std::string &tag)
      : id_(name + ":" + tag) {}

  ImageNameTag(std::string id) : id_(std::move(id)) {}

  ImageNameTag(ImageNameTag &&) = default;

  ImageNameTag &operator=(ImageNameTag &&) = default;

  ImageNameTag(const ImageNameTag &) = default;

  ImageNameTag &operator=(const ImageNameTag &) = default;

  const std::string &id() const { return id_; }

private:
  std::string id_{""};
  friend bool operator<(const ImageNameTag &lhs, const ImageNameTag &rhs);
};

class OciManifest {
public:
  OciManifest() = default;

  OciManifest(registry_manifest_schema2 *manifest) : manifest_(manifest) {}

  ~OciManifest() {
    if (manifest_ != nullptr) {
      free_registry_manifest_schema2(manifest_);
    }
  }

  OciManifest(const OciManifest &) = delete;

  OciManifest &operator=(const OciManifest &) = delete;

  OciManifest(OciManifest &&rhs) : manifest_(rhs.manifest_) {
    rhs.manifest_ = nullptr;
  }

  OciManifest &operator=(OciManifest &&rhs) {
    if (this == &rhs) {
      return *this;
    }
    manifest_ = rhs.manifest_;
    rhs.manifest_ = nullptr;
    return *this;
  }

  auto schema_version() const { return manifest_->schema_version; }

  const char *media_type() const { return manifest_->media_type; }

  auto config() const { return manifest_->config; }

  auto layers() const { return manifest_->layers; }

  auto layers_len() const { return manifest_->layers_len; }

  static std::optional<OciManifest> from_file(const std::string &path);

private:
  registry_manifest_schema2 *manifest_{nullptr};
};

class OciConfig {

public:
  OciConfig() = default;

  OciConfig(oci_image_spec *config) : config_(config) {}

  OciConfig(const OciConfig &) = delete;

  OciConfig &operator=(const OciConfig &) = delete;

  OciConfig(OciConfig &&rhs) : config_(rhs.config_) { rhs.config_ = nullptr; }

  OciConfig &operator=(OciConfig &&rhs) {
    if (this == &rhs) {
      return *this;
    }
    config_ = rhs.config_;
    rhs.config_ = nullptr;
    return *this;
  }

  ~OciConfig() {
    if (config_ != nullptr) {
      free_oci_image_spec(config_);
    }
  }

  const char *created() const { return config_->created; }

  const char *author() const { return config_->author; }

  const char *architecture() const { return config_->architecture; }

  const char *os() const { return config_->os; }

  auto config() const { return config_->config; }

  auto rootfs() const { return config_->rootfs; }

  auto history() const { return config_->history; }

  auto history_len() const { return config_->history_len; }

  static std::optional<OciConfig> from_file(const std::string &path);

private:
  oci_image_spec *config_{nullptr};
};

class ImageMetadata {

public:
  ImageMetadata() = default;

  ImageMetadata(ImageNameTag image, OciConfig config, OciManifest manifest)
      : image_(std::move(image)), config_(std::move(config)),
        manifest_(std::move(manifest)) {}

  const auto &image() { return image_; }

  const auto &config() { return config_; }

  const auto &manifest() { return manifest_; }

private:
  ImageNameTag image_;
  OciConfig config_;
  OciManifest manifest_;
};

namespace hdc {
namespace host {
namespace client {
using hdc::network::tcp::TcpConnectionPtr;
struct OffloadElement {
  std::string image_name_tag;
  TcpConnectionPtr conn;
};

struct UntarData {
  std::string layer_;
  std::string image_name_tag_;
  std::vector<uint8_t> segment_;
  int index_;
  int total_segments_;

  UntarData(std::string layer, std::string image_name_tag,
            std::vector<uint8_t> segment, int index, int total_segments);

  UntarData(UntarData &&) = default;

  UntarData &operator=(UntarData &&) = default;
};

struct UntarResult {
  std::string layer;
  std::string image_name_tag;
  bool success;

  UntarResult(std::string layer, std::string image_name_tag, bool success);

  UntarResult(const UntarResult &) = default;

  UntarResult &operator=(const UntarResult &) = default;

  UntarResult(UntarResult &&) = default;

  UntarResult &operator=(UntarResult &&) = default;
};
} // namespace client
} // namespace host
} // namespace hdc