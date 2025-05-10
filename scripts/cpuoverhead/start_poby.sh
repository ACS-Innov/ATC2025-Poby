#!/bin/bash

source ./scripts/set_env.sh
mkdir -p log/cpuoverhead

mem_num=3
rdma_mem=34603008
doca_mem=34603008


ssh content_server " cd ${POBY_PATH} && mkdir -p log/cpuoverhead && \
    echo ${SUDO_PASSWORD} | sudo -S ./build/src/host/server/content_server \
    -content_server_listen_ip ${CONTENT_SERVER_IP} \
    -content_server_listen_port ${CONTENT_SERVER_PORT} \
    -content_server_registry_path ${CONTENT_SERVER_REGISTRY_PATH} \
    -content_server_ib_dev_name ${CONTENT_SERVER_IB_DEV_NAME} \
    -content_server_rdma_mem ${rdma_mem} \
    -content_server_rdma_mem_num ${mem_num}  \
        > log/cpuoverhead/server.log 2>&1 & "

echo "start content server"
sleep 4s 

ssh dpu "cd ${POBY_PATH} && mkdir -p log/cpuoverhead && \
        echo ${SUDO_PASSWORD} | sudo -S taskset -c 1-7  ./build/src/dpu/dpu_main \
        -decompress_client_peer_ip ${DECOMPRESS_SERVER_IP} \
        -decompress_client_peer_port ${DECOMPRESS_SERVER_PORT} \
        -decompress_client_ib_dev_name ${DECOMPRESS_CLIENT_IB_DEV_NAME} \
        -offload_server_listen_ip ${OFFLOAD_SERVER_IP} \
        -offload_server_listen_port ${OFFLOAD_SERVER_PORT} \
        -offload_server_ib_dev_name ${OFFLOAD_SERVER_IB_DEV_NAME} \
        -content_client_peer_ip ${CONTENT_SERVER_IP} \
        -content_client_peer_port ${CONTENT_SERVER_PORT} \
        -content_client_ib_dev_name ${CONTENT_CLIENT_IB_DEV_NAME} \
        -content_client_rdma_mem ${rdma_mem} \
        -content_client_rdma_mem_num ${mem_num} \
        -decompress_client_doca_mem ${doca_mem} \
        -decompress_client_doca_mem_num ${mem_num} \
        -decompress_client_rdma_mem_num ${mem_num} \
        -decompress_client_rdma_mem 4096 \
        -offload_server_rdma_mem_num ${mem_num} \
        -offload_server_rdma_mem 4096 \
        -blob_size ${doca_mem} \
            > log/cpuoverhead/dpu.log 2>&1 & "

echo "start dpu server"
sleep 4s

echo ${SUDO_PASSWORD} | sudo -S taskset -c 5-5 ./build/src/host/client/client_main \
    -command_server_ip ${POBY_CLI_IP} \
    -command_server_port ${POBY_CLI_PORT} \
    -decompress_server_listen_ip ${DECOMPRESS_SERVER_IP} \
    -decompress_server_listen_port ${DECOMPRESS_SERVER_PORT} \
    -decompress_server_ib_dev_name ${DECOMPRESS_SERVER_IB_DEV_NAME} \
    -offload_client_peer_ip  ${OFFLOAD_SERVER_IP} \
    -offload_client_peer_port ${OFFLOAD_SERVER_PORT} \
    -offload_client_ib_dev_name ${OFFLOAD_CLIENT_IB_DEV_NAME} \
    -offload_client_rdma_mem_num ${mem_num} \
    -offload_client_rdma_mem 4096 \
    -decompress_server_untar_num_threads 1 \
    -decompress_server_doca_mem_num ${mem_num} \
    -decompress_server_doca_mem ${doca_mem} \
    -decompress_server_rdma_mem_num ${mem_num} \
    -decompress_server_rdma_mem 4096 \
        >log/cpuoverhead/client.log  2>&1 &

echo "start cpu server"
sleep 4s
