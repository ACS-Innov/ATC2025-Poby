add_executable(content_server content_server.cc ${PROTO_CODE_SRCS})
target_link_libraries(
  content_server PUBLIC network ${GFLAGS_LIBRARY} spdlog::spdlog ${DYNAMIC_LIB}
                        third_party_isulad)
target_compile_options(content_server
                       PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
