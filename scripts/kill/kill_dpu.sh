#!/bin/bash
set -x

source ./scripts/set_env.sh

pids=$(ps aux | grep 'dpu_main' | grep -v grep | awk '{print $2}')

if [ -z "$pids" ]; then
    echo "not found dpu process."
else
    for pid in $pids; do
        echo ${SUDO_PASSWORD} | sudo -S kill -9 "$pid"
    done
    echo "kill dpu process"
fi