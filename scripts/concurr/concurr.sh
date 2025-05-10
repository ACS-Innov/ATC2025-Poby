#!/bin/bash 

source ./scripts/set_env.sh

mkdir -p log/concurr
concurr_num=2

# start poby
./scripts/concurr/start_poby.sh 


images=(
social-network-microservices
movie-rent-service
hotel-reservation
python3
nodejs
golang
php
pyaes
linpack
chameleon
matmul
image-processing
)

# remove tmp result files
echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*

# warm up
./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name nodejs  --image_tag 0 

# remove tmp result files
echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*


for ((i=0;i<${concurr_num};i++)); do

    mkdir -p log/concurr/${concurr_num}/curr_${i}/
    {
        num=0
        for image in ${images[@]}; do
            ./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name ${image}  --image_tag ${i} \
                > log/concurr/${concurr_num}/curr_${i}/cli_${num}.log 2>&1
            ((num+=1))
        done 
    } &
done
