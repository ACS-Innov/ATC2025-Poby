# get current directory sources files
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} src_utils_top_srcs)

set(local_src_utils_srcs ${src_utils_top_srcs})

set(local_src_utils_incs ${CMAKE_CURRENT_SOURCE_DIR})

set(SRC_UTILS_SRCS
    ${local_src_utils_srcs}
    PARENT_SCOPE)
set(SRC_UTILS_INCS
    ${local_src_utils_incs}
    PARENT_SCOPE)

add_library(src_utils OBJECT ${local_src_utils_srcs})
target_include_directories(src_utils PUBLIC ${local_src_utils_incs})
target_link_libraries(src_utils PUBLIC spdlog::spdlog)
