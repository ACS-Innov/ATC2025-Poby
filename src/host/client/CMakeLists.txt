if(DOCA_FOUND)
  add_executable(
    client_main
    client_main.cc
    command_server.cc
    offload_client_epoll.cc
    decompress_server_epoll.cc
    untar_engine.cc
    metadata.cc
    ${PROTO_CODE_SRCS})
  target_include_directories(client_main PUBLIC ${DOCA_INCLUDE_DIRS})
  target_link_libraries(
    client_main
    PUBLIC compress
          #  dma
           image_ops
           spdlog::spdlog
           tl::expected
           network
           ${GFLAGS_LIBRARY}
           ${DYNAMIC_LIB}
           ${FOLLY_LIBRARIES}
           ${FOLLY_FMT_LIBRARIES})
  target_link_directories(client_main PRIVATE ${FOLLY_LIBRARY_DIRS}
                          ${FOLLY_FMT_LIBRARY_DIRS})
  target_compile_options(
    client_main
    PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE"
    PRIVATE ${FOLLY_CFLAGS})

  add_executable(client_cli client_cli.cc ${PROTO_CODE_SRCS})
  target_link_libraries(client_cli PUBLIC network ${GFLAGS_LIBRARY}
                                          spdlog::spdlog ${DYNAMIC_LIB})
endif()
