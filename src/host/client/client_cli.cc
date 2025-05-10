#include "container.pb.h"
#include "network/EventLoop.h"
#include "network/InetAddress.h"
#include "network/tcp/TcpClient.h"
#include <gflags/gflags.h>
#include <host/client/metadata.h>
#include <spdlog/spdlog.h>
#include <utils/MsgFrame.h>
using container::CreateContainerRequest;
using container::CreateContainerResponse;
using hdc::network::EventLoop;
using hdc::network::InetAddress;
DEFINE_string(command_peer_ip, "10.16.0.186",
              "The ip address of command server");
DEFINE_int32(command_peer_port, 9000, "The port of command server");
DEFINE_string(image_name, "", "Image name to pull");
DEFINE_string(image_tag, "latest", "The tag of image");

using namespace hdc::network;
using namespace hdc::network::tcp;
class CommandClient {
public:
  CommandClient(const CommandClient &) = delete;

  CommandClient &operator=(const CommandClient &) = delete;

  CommandClient(EventLoop *event_loop, const InetAddress &serverAddr,
                const std::string &name)
      : client_(event_loop, serverAddr, name) {
    client_.setConnectionCallback(
        [this](const TcpConnectionPtr &conn) { this->OnConnection(conn); });

    client_.setMessageCallback([this](const TcpConnectionPtr &conn,
                                      Buffer *buffer, Timestamp timestamp) {
      this->OnMessage(conn, buffer, timestamp);
    });
  }

  void setRequest(container::CreateContainerRequest request) {
    request_ = std::move(request);
  }

  void connect() { client_.connect(); }

private:
  TcpClient client_;
  container::CreateContainerRequest request_;

  void OnConnection(const TcpConnectionPtr &conn) {
    SPDLOG_DEBUG("{} new connection: {} -> {} is {}", client_.name(),
                 conn->peerAddress().toIpPort(),
                 conn->localAddress().toIpPort(),
                 (conn->connected() ? "UP" : "DOWN"));
    if (conn->connected()) {
      SPDLOG_INFO("Send CreateContainerRequest. image: {}",
                  request_.image_name_tag());
      sendTcpPbMsg(conn, request_);
    }
  }

  void OnMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                 Timestamp timestamp) {
    container::CreateContainerResponse response;
    if (receiveTcpPbMsg(buffer, response) == false) {
      return;
    }
    if (!response.success()) {
      SPDLOG_ERROR("pull {} fail.", request_.image_name_tag());
      return;
    }
    SPDLOG_INFO("Recv CreateContainerResponse. image: {}, path: {}",
                request_.image_name_tag(), response.path());
    SPDLOG_INFO("the provision of container {} is done", request_.image_name_tag());
    conn->shutdown();
    conn->getLoop()->quit();
  }
};

int main(int argc, char **argv) {
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("%^[%L][%T.%e]%$[%s:%#] %v");

  SPDLOG_DEBUG("start pulling {}:{}", FLAGS_image_name, FLAGS_image_tag);

  auto image = ImageNameTag{FLAGS_image_name, FLAGS_image_tag};

  auto request = CreateContainerRequest();
  request.set_image_name_tag(image.id());
  auto loop = EventLoop();
  auto server_addr =
      InetAddress(FLAGS_command_peer_ip, FLAGS_command_peer_port);
  auto client = CommandClient(&loop, server_addr, "CommandCli");

  client.setRequest(std::move(request));
  client.connect();
  loop.loop();
}