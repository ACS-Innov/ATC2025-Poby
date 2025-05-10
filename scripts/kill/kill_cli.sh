#!/bin/bash

source ./scripts/set_env.sh
pids=$(ps aux | grep 'client_cli' | grep -v grep | awk '{print $2}')

if [ -z "$pids" ]; then
    echo "not found client_cli process."
else
    for pid in $pids; do
        echo ${SUDO_PASSWORD} | sudo -S kill -9 "$pid"
    done
    echo "kill client_cli process"
fi