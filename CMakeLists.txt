cmake_minimum_required(VERSION 3.1...3.24)
project(DMA_Bench VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES
                                              "amd64")
  message(STATUS "Detected x86_64 architecture")
  set(ENV{PKG_CONFIG_PATH}
      "/opt/mellanox/dpdk/lib/x86_64-linux-gnu/pkgconfig/:/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig/:/opt/mellanox/flexio/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
  )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES
                                               "aarch64")
  message(STATUS "Detected ARM architecture")
  set(ENV{PKG_CONFIG_PATH}
      "/opt/mellanox/dpdk/lib/aarch64-linux-gnu/pkgconfig/:/opt/mellanox/doca/lib/aarch64-linux-gnu/pkgconfig/:/opt/mellanox/flexio/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
  )
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
message(STATUS "PKG_CONFIG_PATH=$ENV{PKG_CONFIG_PATH}")
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party)
find_package(PkgConfig REQUIRED)
include(FetchContent)

# DOCA
pkg_check_modules(DOCA doca)
if(DOCA_FOUND)
  include_directories(${DOCA_INCLUDE_DIRS})
else()
  message(WARNING "DOCA not found")
endif()
# RDMA
pkg_check_modules(RDMA REQUIRED libibverbs)
include_directories(${RDMA_INCLUDE_DIRS})
# spdlog
find_package(spdlog CONFIG REQUIRED)
# tl-expected
find_package(tl-expected CONFIG REQUIRED)
# leveldb
find_package(leveldb CONFIG REQUIRED)
include(FindThreads)
# protobuf
include(FindProtobuf)
if(PROTOBUF_FOUND)
  message("Found protobuf version: ${Protobuf_VERSION}")
else()
  message("protobuf not found")
endif()
# archive
find_package(LibArchive REQUIRED)
# devmapper
pkg_check_modules(DEVMAPPER REQUIRED devmapper)
include_directories(${DEVMAPPER_INCLUDE_DIRS})
# http_parse_dev
find_path(HTTP_PARSER_INCLUDE_DIR http_parser.h)
find_library(HTTP_PARSER_LIBRARY http_parser)
# libcurl_dev
pkg_check_modules(CURL REQUIRED libcurl)
include_directories(${CURL_INCLUDE_DIRS})
# yajl
pkg_check_modules(YAJL REQUIRED yajl)
include_directories(${YAJL_INCLUDE_DIRS})
# check libcapability
pkg_check_modules(LIBCAP REQUIRED libcap)
include_directories(${LIBCAP_INCLUDE_DIRS})

# generate protobuf
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/pb/generated)

file(GLOB protobuf_files ${CMAKE_SOURCE_DIR}/pb/*.proto)
set(PROTO_META_BASE_DIR ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND PROTO_FLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/pb)

set(PROTO_CODE_SRCS "")
foreach(FIL ${protobuf_files})

  get_filename_component(FIL_WE ${FIL} NAME_WE)

  string(REGEX REPLACE ".+/(.+)\\..*" "\\1" FILE_NAME ${FIL})
  string(REGEX REPLACE "(.+)\\${FILE_NAME}.*" "\\1" FILE_PATH ${FIL})
  set(FILE_PATH "${FILE_PATH}/generated")

  set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.cc")
  set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${FIL_WE}.pb.h")
  execute_process(COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS}
                          --cpp_out=${PROTO_META_BASE_DIR} ${FIL})
  file(COPY ${PROTO_SRCS} DESTINATION ${FILE_PATH})
  list(APPEND PROTO_CODE_SRCS "${FILE_PATH}/${FIL_WE}.pb.cc")
  file(COPY ${PROTO_HDRS} DESTINATION ${FILE_PATH})

endforeach()
include_directories("${CMAKE_SOURCE_DIR}/pb/generated")

# gflags
find_path(GFLAGS_INCLUDE_PATH gflags/gflags.h)
find_library(GFLAGS_LIBRARY NAMES gflags libgflags)
if((NOT GFLAGS_INCLUDE_PATH) OR (NOT GFLAGS_LIBRARY))
  message(FATAL_ERROR "Fail to find gflags")
endif()
include_directories(${GFLAGS_INCLUDE_PATH})
execute_process(
  COMMAND
    bash -c
    "grep \"namespace [_A-Za-z0-9]\\+ {\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $2}' | tr -d '\n'"
  OUTPUT_VARIABLE GFLAGS_NS)
if(${GFLAGS_NS} STREQUAL "GFLAGS_NAMESPACE")
  execute_process(
    COMMAND
      bash -c
      "grep \"#define GFLAGS_NAMESPACE [_A-Za-z0-9]\\+\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $3}' | tr -d '\n'"
    OUTPUT_VARIABLE GFLAGS_NS)
endif()

# folly

set(FOLLY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/folly")
if(NOT EXISTS ${FOLLY_SOURCE_DIR})
  FetchContent_Declare(
    folly
    GIT_REPOSITORY https://github.com/facebook/folly.git
    GIT_TAG v2023.09.11.00
    SOURCE_DIR "${FOLLY_SOURCE_DIR}")
  FetchContent_GetProperties(folly)
  if(NOT folly_POPULATED)
    FetchContent_Populate(folly)
    message("folly_POPULATED: folly_SOURCE_DIR is ${folly_SOURCE_DIR}")
    execute_process(
      COMMAND python3 ./build/fbcode_builder/getdeps.py --allow-system-packages
              build --install-prefix "${folly_SOURCE_DIR}/install"
      WORKING_DIRECTORY ${folly_SOURCE_DIR}
      RESULT_VARIABLE result)
    if(result)
      message(FATAL_ERROR "Failed to build and install Folly.")
    else()
      message("Folly built and installed successfully.")
    endif()
  endif()
endif()

set(FOLLY_INSTALL_DIR "${FOLLY_SOURCE_DIR}/install")
set(ENV{PKG_CONFIG_PATH}
    "${FOLLY_INSTALL_DIR}/folly/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

pkg_check_modules(FOLLY REQUIRED libfolly)
include_directories(${FOLLY_INCLUDE_DIRS})

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES
                                              "amd64")
  message(STATUS "Detected x86_64 architecture")
  set(FOLLY_LIBRARIES
      "${FOLLY_LIBRARIES} /usr/lib/x86_64-linux-gnu/libdouble-conversion.so /usr/lib/x86_64-linux-gnu/libglog.so /usr/lib/x86_64-linux-gnu/libevent.so /usr/lib/x86_64-linux-gnu/libz.so /usr/lib/x86_64-linux-gnu/libssl.so /usr/lib/x86_64-linux-gnu/libcrypto.so /usr/lib/x86_64-linux-gnu/libbz2.so /usr/lib/x86_64-linux-gnu/liblzma.so /usr/lib/x86_64-linux-gnu/liblz4.so /usr/lib/x86_64-linux-gnu/libzstd.so /usr/lib/x86_64-linux-gnu/libsnappy.so /usr/lib/x86_64-linux-gnu/libsodium.so -ldl /usr/lib/x86_64-linux-gnu/libunwind.so /lib/x86_64-linux-gnu/libgflags.so.2.2"
  )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES
                                               "aarch64")
  message(STATUS "Detected ARM architecture")
  set(FOLLY_LIBRARIES
      "${FOLLY_LIBRARIES} /usr/lib/aarch64-linux-gnu/libdouble-conversion.so /usr/lib/aarch64-linux-gnu/libglog.so /usr/lib/aarch64-linux-gnu/libevent.so /usr/lib/aarch64-linux-gnu/libz.so /usr/lib/aarch64-linux-gnu/libssl.so /usr/lib/aarch64-linux-gnu/libcrypto.so /usr/lib/aarch64-linux-gnu/libbz2.so /usr/lib/aarch64-linux-gnu/liblzma.so /usr/lib/aarch64-linux-gnu/liblz4.so /usr/lib/aarch64-linux-gnu/libzstd.so /usr/lib/aarch64-linux-gnu/libsnappy.so /usr/lib/aarch64-linux-gnu/libsodium.so -ldl /usr/lib/aarch64-linux-gnu/libunwind.so.8 /lib/aarch64-linux-gnu/libgflags.so.2.2"
  )
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

file(GLOB FOLLY_INSTALL_SUBDIRECTORIES "${FOLLY_INSTALL_DIR}/*" DIRECTORY)
foreach(SUBDIRECTORY ${FOLLY_INSTALL_SUBDIRECTORIES})
  get_filename_component(DIRNAME ${SUBDIRECTORY} NAME)
  message("dir " ${DIRNAME})
  string(FIND "${DIRNAME}" "fmt" SUBSTR_INDEX)

  if(SUBSTR_INDEX GREATER -1)
    message("Found 'fmt' in: ${SUBDIRECTORY}")
    set(FOLLY_FMT_INSTALL_DIR "${SUBDIRECTORY}")
    break()
  endif()
endforeach()
set(ENV{PKG_CONFIG_PATH}
    "${FOLLY_FMT_INSTALL_DIR}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(FOLLY_FMT REQUIRED fmt)

# project lcr parse manifest & config, alse need by iSulad
include(ExternalProject)
ExternalProject_Add(
  lcr
  GIT_REPOSITORY https://gitee.com/openeuler/lcr.git
  GIT_TAG v2.1.2
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lcr/src
  UPDATE_COMMAND ""
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  CMAKE_ARGS -DENABLE_LIBLCR=OFF
             -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/third_party/lcr)
set(ISULAD_LIBRARIES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lcr/lib/libisula_libutils.a")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/lcr/include)
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/lcr/src/src/utils)
# project iSulad
include(FetchContent)
FetchContent_Declare(
  iSulad
  GIT_REPOSITORY https://gitee.com/zihaochang/iSulad.git
  GIT_TAG v2.1.3_doca
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/iSulad)
set(FETCHCONTENT_UPDATES_DISCONNECT TRUE)
FetchContent_Populate(iSulad)

include_directories(${CMAKE_SOURCE_DIR}/src/)

# CXX_FLAGS
set(CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -g -DGFLAGS_NS=${GFLAGS_NS} -fno-omit-frame-pointer -fPIC -DDOCA_ALLOW_EXPERIMENTAL_API"
)
set(CMAKE_CXX_FLAGS_RELEASE
    "${CMAKE_CXX_FLAGS_RELEASE} -g -O2 -DNDEBUG  -DGFLAGS_NS=${GFLAGS_NS} -fno-omit-frame-pointer -fPIC -DDOCA_ALLOW_EXPERIMENTAL_API"
)
# dynamic lib
set(DYNAMIC_LIB ${CMAKE_THREAD_LIBS_INIT} ${GFLAGS_LIBRARY}
                ${PROTOBUF_LIBRARIES} ${LEVELDB_LIBRARIES} dl)

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/iSulad/src/utils")
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/iSulad/src/common")
add_subdirectory(
  "${CMAKE_SOURCE_DIR}/third_party/iSulad/src/daemon/modules/image")
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/iSulad/src/daemon/common")
add_subdirectory(
  "${CMAKE_SOURCE_DIR}/third_party/iSulad/src/daemon/modules/api")
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/iSulad/src/daemon/config")
add_subdirectory("${CMAKE_SOURCE_DIR}/src/host")
add_subdirectory("${CMAKE_SOURCE_DIR}/src/doca")
add_subdirectory("${CMAKE_SOURCE_DIR}/src/dpu")
add_subdirectory("${CMAKE_SOURCE_DIR}/src/network")
add_subdirectory("${CMAKE_SOURCE_DIR}/src/image")
add_subdirectory("${CMAKE_SOURCE_DIR}/src/utils")
add_subdirectory("${CMAKE_SOURCE_DIR}/tool")
if(NOT ISULAD_ROOT_PATH)
  add_definitions(-DISULAD_ROOT_PATH="/var/lib/isulad")
endif()

if(NOT ISULAD_STATE_PATH)
  add_definitions(-DISULAD_STATE_PATH="/var/run/isulad")
endif()

if(NOT ISULAD_STATE_PATH)
  add_definitions(-DISULAD_STATE_PATH="/var/run/isulad")
endif()

if(NOT DISABLE_CLEANUP)
  add_definitions(-DDISABLE_CLEANUP)
endif()

if(NOT RUNPATH)
  add_definitions(-DRUNPATH="/var/run")
endif()

if(NOT SYSCONFDIR_PREFIX)
  add_definitions(-DSYSCONFDIR_PREFIX="")
endif()

add_library(
  third_party_isulad STATIC
  ${SRC_IMAGE_SRCS}
  ${UTILS_SRCS}
  ${COMMON_SRCS}
  ${IMAGE_SRCS}
  ${DAEMON_COMMON_SRCS}
  ${MODULES_API_SRCS}
  ${CONFIG_SRCS})
target_include_directories(
  third_party_isulad
  PUBLIC ${UTILS_INCS}
         ${COMMON_INCS}
         ${IMAGE_INCS}
         ${DAEMON_COMMON_INCS}
         ${MODULES_API_INCS}
         ${CONFIG_INCS}
         ${SRC_IMAGE_INCS})
target_link_libraries(
  third_party_isulad
  PUBLIC LibArchive::LibArchive
         spdlog::spdlog
         ${ISULAD_LIBRARIES}
         ${YAJL_LIBRARIES}
         ${CURL_LIBRARIES}
         ${DEVMAPPER_LIBRARIES}
         ${HTTP_PARSER_LIBRARY}
         ${LIBCAP_LIBRARIES}
         src_utils)

add_subdirectory("${CMAKE_SOURCE_DIR}/src/test")