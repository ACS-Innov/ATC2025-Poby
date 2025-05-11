#!/bin/bash

source ./scripts/set_env.sh

mkdir -p log/e2e

# start poby
./scripts/e2e/start_poby.sh 

# remove tmp result files
echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*


# warm up
./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name nodejs  --image_tag 0 > log/e2e/cli_warmup.log 2>&1
# remove tmp result files
echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*


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

num=0

echo "start pulling images"

for image in ${images[@]}; do
     ./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name ${image}  --image_tag 0 \
        > log/e2e/cli_${num}.log 2>&1
    
    sleep 5s
    ((num+=1))
done

echo "end pulling images"
./scripts/remote_kill.sh 

# extract time from logs 

echo "Poby E2E test result:"
folder=log/
num=0
for image in ${images[@]}; do
    log_file="log/e2e/cli_${num}.log"
    if [[ -f $log_file ]] ; then 
        start_time=$(grep "start pulling" "$log_file" | sed -n 's/.*\[\([0-9:.]*\)\].*/\1/p')
        end_time=$(grep "the provision of container" "$log_file" | sed -n 's/.*\[\([0-9:.]*\)\].*/\1/p')

        start_seconds=$(echo "$start_time" | awk -F: '{print ($1*3600 + $2*60 + $3)}')
        end_seconds=$(echo "$end_time" | awk -F: '{print ($1*3600 + $2*60 + $3)}')

        time_diff=$(printf "%.3f" $(echo "scale=3; $end_seconds - $start_seconds" | bc))


        echo "${image}: ${time_diff}s" 
    fi
    ((num+=1))
done