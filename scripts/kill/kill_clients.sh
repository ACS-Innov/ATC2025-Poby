#!/bin/bash

source ./scripts/set_env.sh

pids=$(ps aux | grep 'offload_client' | grep -v grep | awk '{print $2}')

if [ -z "$pids" ]; then
    echo "not found client process."
else
    for pid in $pids; do
        echo ${SUDO_PASSWORD} | sudo -S kill -9 "$pid"
    done
    echo "kill client process"
fi