if(DOCA_FOUND)
  add_executable(image_compress image_compress.cc)
  target_include_directories(image_compress PUBLIC ${DOCA_INCLUDE_DIRS})
  target_link_libraries(
    image_compress
    PUBLIC compress
           spdlog::spdlog
           tl::expected
           network
           ${FOLLY_LIBRARIES}
           ${FOLLY_FMT_LIBRARIES})
  target_link_directories(image_compress PUBLIC ${DOCA_LIBRARY_DIRS}
                          ${FOLLY_LIBRARY_DIRS} ${FOLLY_FMT_LIBRARY_DIRS})
  target_compile_options(
    image_compress PUBLIC "-D DOCA_ALLOW_EXPERIMENTAL_API"
                          "-D SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
endif()
