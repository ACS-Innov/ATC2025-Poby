#include <host/client/metadata.h>

bool operator<(const ImageNameTag &lhs, const ImageNameTag &rhs) {
  return lhs.id_ < rhs.id_;
}

std::optional<OciManifest> OciManifest::from_file(const std::string &path) {
  auto in_file = std::ifstream{path.data()};
  if (!in_file) {
    SPDLOG_ERROR("Fail to open file {}", path);
    return std::nullopt;
  }
  in_file.seekg(0, std::ios::end);
  auto in_file_size = in_file.tellg();
  in_file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(in_file_size);
  in_file.read(reinterpret_cast<char *>(data.data()), in_file_size);
  in_file.close();
  data.push_back(0);
  auto manifest =
      oci_parse_manifest_schema2(reinterpret_cast<char *>(data.data()));
  if (manifest == nullptr) {
    SPDLOG_ERROR("parse oci_manifest error");
    return std::nullopt;
  }
  return OciManifest{manifest};
}

std::optional<OciConfig> OciConfig::from_file(const std::string &path) {
  auto in_file = std::ifstream{path.data()};
  if (!in_file) {
    SPDLOG_ERROR("Fail to open file {}", path);
    return std::nullopt;
  }
  in_file.seekg(0, std::ios::end);
  auto in_file_size = in_file.tellg();
  in_file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(in_file_size);
  in_file.read(reinterpret_cast<char *>(data.data()), in_file_size);
  in_file.close();
  data.push_back(0);

  auto config = parse_oci_config(reinterpret_cast<char *>(data.data()));
  if (config == nullptr) {
    SPDLOG_ERROR("parse_oci_config error");
    return std::nullopt;
  }
  return OciConfig{config};
}

namespace hdc {
namespace host {
namespace client {
UntarData::UntarData(std::string layer, std::string image_name_tag,
                     std::vector<uint8_t> segment, int index,
                     int total_segments)
    : layer_(std::move(layer)), image_name_tag_(std::move(image_name_tag)),
      segment_(std::move(segment)), index_(index),
      total_segments_(total_segments) {}

UntarResult::UntarResult(std::string layer, std::string image_name_tag,
                         bool success)
    : layer(std::move(layer)), image_name_tag(std::move(image_name_tag)),
      success(success) {}
} // namespace client
} // namespace host
} // namespace hdc