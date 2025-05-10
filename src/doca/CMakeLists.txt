if(DOCA_FOUND)
  add_library(compress STATIC doca_buf.cc common.cc params.cc core.cc compress.cc)
  target_link_libraries(compress ${DOCA_LIBRARIES} spdlog::spdlog)
  target_link_directories(compress PUBLIC ${DOCA_LIBRARY_DIRS})

  # add_library(dma STATIC common.cc params.cc core.cc dma.cc)
  # target_link_libraries(dma ${DOCA_LIBRARIES} spdlog::spdlog)
  # target_link_directories(dma PUBLIC ${DOCA_LIBRARY_DIRS})
endif()
