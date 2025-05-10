#!/bin/bash

source ./scripts/set_env.sh

pids=$(ps aux | grep 'content_server' | grep -v grep | awk '{print $2}')

if [ -z "$pids" ]; then
    echo "not found server process."
else
    for pid in $pids; do
        echo ${SUDO_PASSWORD} | sudo -S kill -9 "$pid"
    done
    echo "kill server process"
fi