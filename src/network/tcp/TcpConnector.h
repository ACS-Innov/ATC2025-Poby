// Inspired by Muduo https://github.com/chenshuo/muduo
#pragma once
#include <network/InetAddress.h>
#include <fmt/core.h>
#include <functional>
#include <memory>
namespace hdc {
namespace network {
class Channel;
class EventLoop;

namespace tcp {

class TcpConnector : public std::enable_shared_from_this<TcpConnector> {
public:
  TcpConnector(const TcpConnector &) = delete;
  TcpConnector &operator=(const TcpConnector &) = delete;
  typedef std::function<void(int sockfd)> NewConnectionCallback;

  TcpConnector(EventLoop *loop, const InetAddress &serverAddr);
  ~TcpConnector();

  void setNewConnectionCallback(const NewConnectionCallback &cb) {
    newConnectionCallback_ = cb;
  }

  void start();   // can be called in any thread
  void restart(); // must be called in loop thread
  void stop();    // can be called in any thread

  const InetAddress &serverAddress() const { return serverAddr_; }

private:
  enum class States { kDisconnected, kConnecting, kConnected };
  friend class fmt::formatter<States>;
  static const int kMaxRetryDelayMs = 30 * 1000;
  static const int kInitRetryDelayMs = 500;

  void setState(States s) { state_ = s; }
  void startInLoop();
  void stopInLoop();
  void connect();
  void connecting(int sockfd);
  void handleWrite();

  /// @Regitster to Channel for handling error event.
  void handleError();
  void retry(int sockfd);
  int removeAndResetChannel();
  void resetChannel();

  EventLoop *loop_;
  InetAddress serverAddr_;
  bool connect_; // atomic
  States state_; // FIXME: use atomic variable
  std::unique_ptr<Channel> channel_;
  NewConnectionCallback newConnectionCallback_;
  int retryDelayMs_;
};
} // namespace tcp
} // namespace network
} // namespace hdc
enum class color { red, green, blue };

template <>
struct fmt::formatter<hdc::network::tcp::TcpConnector::States>
    : formatter<string_view> {
  // parse is inherited from formatter<string_view>.

  auto format(hdc::network::tcp::TcpConnector::States c,
              format_context &ctx) const;
};
