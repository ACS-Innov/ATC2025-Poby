if(DOCA_FOUND)
  add_executable(
    dpu_main dpu_main.cc content_fetcher.cc decompress_client_epoll.cc
             offload_server_epoll.cc ${PROTO_CODE_SRCS})
  target_link_libraries(
    dpu_main
    PUBLIC compress
          #  dma
           spdlog::spdlog
           tl::expected
           network
           ${GFLAGS_LIBRARY}
           ${DYNAMIC_LIB}
           ${FOLLY_LIBRARIES}
           ${FOLLY_FMT_LIBRARIES}
           )
  target_include_directories(dpu_main PUBLIC ${DOCA_INCLUDE_DIRS})
  target_link_directories(dpu_main PUBLIC ${DOCA_LIBRARY_DIRS} PRIVATE
                          ${FOLLY_LIBRARY_DIRS} ${FOLLY_FMT_LIBRARY_DIRS})
  target_compile_options(
    dpu_main
    PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE"
    PRIVATE ${FOLLY_CFLAGS})

endif()
