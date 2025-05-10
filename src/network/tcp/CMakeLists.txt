add_library(tcp
SocketsOps.cc
InetAddress.cc
Socket.cc
)
target_link_libraries(tcp tl::expected spdlog::spdlog)