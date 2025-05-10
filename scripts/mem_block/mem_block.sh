#!/bin/bash

source ./scripts/set_env.sh
mem_blocks=(1 2 3 4 5 6)
rdma_mems=(5242880 9437184 17825792 34603008 68157440 135266304)
doca_mems=(5242880 9437184 17825792 34603008 68157440 134217728)
server_mems=(4 8 16 32 64 128)

mkdir -p log/mem_block

# It is recommended to change image maunally when running this script
images=(
social-network-microservices
# movie-rent-service
# hotel-reservation
# python3
# nodejs
# golang
# php
# pyaes
# linpack
# chameleon
# matmul
# image-processing
)

for ((i=0; i<6; i++)); do
    for ((j=0; j<6; j++)); do
        mkdir log/${mem_blocks[$i]}_${server_mems[$j]}

        if (( ${server_mems[$j]} == 128 )); then
            blob_size=0
        else 
            blob_size=${doca_mems}
        fi 
        
        ./scripts/mem_block/start_poby.sh ${mem_blocks[$i]} ${rdma_mems[$j]} ${doca_mems[$j]} ${server_mems[$j]} ${blob_size} 
        
        echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*


        num=0
        for ((k=0; k < 10; k++)); do
        for image in ${images[@]}; do
            ./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name ${image}  --image_tag 0 \
                > log/mem_block/${mem_blocks[$i]}_${server_mems[$j]}/cli_${mem_blocks[$i]}_${server_mems[$j]}_${num}.log 2>&1
	    echo "${image}: mem_blocks(${mem_blocks[$i]})-server_mems(${server_mems[$j]}) done."
            
            sleep 3s
            ((num+=1))
        done
        done
        ./scripts/remote_kill.sh 

        sleep 10s
    done
done
