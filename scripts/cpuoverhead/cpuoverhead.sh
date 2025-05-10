#!/bin/bash

source ./scripts/set_env.sh

mkdir -p log/cpuoverhead

# start poby
./scripts/cpuoverhead/start_poby.sh 

image="image-processing"
x=0

# remove tmp result files
echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*

# warm up
./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name nodejs --image_tag ${x}

# remove tmp result files
echo ${SUDO_PASSWORD} | sudo -S rm -rf untar/design/*

sleep 1s


top 1 -d 0.1 -b | grep "Cpu5  :" > log/cpuoverhead/cpu_overhead.log &
pstat_pid=$!


command="./build/src/host/client/client_cli -command_peer_ip ${POBY_CLI_IP} -command_peer_port ${POBY_CLI_PORT} -image_name ${image}  --image_tag ${x}"
output_file="log/cpuoverhead/cli_${x}.log"
$command > $output_file 2>&1

sleep 5s
echo ${SUDO_PASSWORD} | sudo -S kill -9 $pstat_pid

./scripts/remote_kill.sh 