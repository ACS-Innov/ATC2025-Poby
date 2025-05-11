#!/bin/bash
# set -x
source ./scripts/set_env.sh


./scripts/kill/kill_clients.sh  
./scripts/kill/kill_cli.sh

ssh dpu "cd ${POBY_PATH} && \
      ./scripts/kill/kill_dpu.sh"