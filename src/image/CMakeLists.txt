# get current directory sources files
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} local_image_srcs)

set(SRC_IMAGE_SRCS
    ${local_image_srcs}
    PARENT_SCOPE)

set(SRC_IMAGE_INCS
    ${CMAKE_CURRENT_SOURCE_DIR}
    PARENT_SCOPE)

add_library(image_ops STATIC ${local_image_srcs})
target_include_directories(image_ops PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(image_ops PUBLIC third_party_isulad)
