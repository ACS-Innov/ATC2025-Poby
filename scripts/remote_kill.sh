#!/bin/bash
# set -x
source ./scripts/set_env.sh

./scripts/kill/kill_all.sh  

ssh content_server "cd ${POBY_PATH} && \
      ./scripts/kill/kill_content_server.sh"
