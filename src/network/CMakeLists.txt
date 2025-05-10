add_subdirectory("tcp/")


add_library(network
STATIC 
Channel.cc
CountDownLatch.cc 
CurrentThread.cc 
EventLoop.cc 
EventLoopThread.cc 
EventLoopThreadPool.cc 
Poller.cc 
Thread.cc 
Timer.cc 
TimerQueue.cc 
Timestamp.cc 

tcp/Acceptor.cc 
tcp/Buffer.cc 
tcp/InetAddress.cc 
tcp/Socket.cc 
tcp/SocketsOps.cc 
tcp/TcpClient.cc 
tcp/TcpConnection.cc 
tcp/TcpConnector.cc 
tcp/TcpServer.cc

rdma/RdmaConnection.cc 
rdma/RdmaConnector.cc 
rdma/RdmaClient.cc
rdma/RdmaServer.cc 
rdma/DevContext.cc
)
target_link_libraries(network
spdlog::spdlog
${RDMA_LIBRARIES})
target_link_directories(network
PRIVATE ${RDMA_LIBRARY_DIRS}
)
target_compile_options(network PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")

