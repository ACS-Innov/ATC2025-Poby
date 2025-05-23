#!/bin/bash

# change those variables
POBY_PATH=/home/atc25-ae/ATC2025-Poby
SUDO_PASSWORD=poby
REGISTRY_MIRROR=10.16.0.183:5000

POBY_CLI_IP=10.16.0.185
POBY_CLI_PORT=60000

CONTENT_SERVER_IP=10.64.100.184
CONTENT_SERVER_PORT=9002
CONTENT_SERVER_IB_DEV_NAME=mlx5_0
CONTENT_CLIENT_IB_DEV_NAME=mlx5_3
CONTENT_SERVER_REGISTRY_PATH="data/registry/content_layers_32m/"


DECOMPRESS_SERVER_IP=172.24.56.185
DECOMPRESS_SERVER_PORT=60001
DECOMPRESS_SERVER_IB_DEV_NAME=mlx5_0
DECOMPRESS_CLIENT_IB_DEV_NAME=mlx5_2

OFFLOAD_SERVER_IP=172.24.56.85
OFFLOAD_SERVER_PORT=60003
OFFLOAD_SERVER_IB_DEV_NAME=mlx5_2
OFFLOAD_CLIENT_IB_DEV_NAME=mlx5_0

#########################