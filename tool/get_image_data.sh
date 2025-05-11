#!/bin/bash

source ./scripts/set_env.sh

# Define download directory

while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--download-dir)
            download_dir="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [-d|--download-dir <directory>]"
            exit 1
            ;;
    esac
done

# Set default value if not provided
if [ -z "$download_dir" ]; then
    download_dir="./data/image_raw"
fi

# Create log directory if it does not exist
if [ ! -d "$download_dir" ]; then
    mkdir -p "$download_dir"
    echo "Created directory: $download_dir"
fi

images=(
${REGISTRY_MIRROR}/deathstarbench/social-network-microservices:latest
${REGISTRY_MIRROR}/yg397/media-microservices:latest
${REGISTRY_MIRROR}/deathstarbench/hotel-reservation:latest
${REGISTRY_MIRROR}/openwhisk/python3action:latest
${REGISTRY_MIRROR}/openwhisk/action-nodejs-v10:latest
${REGISTRY_MIRROR}/openwhisk/actionloop-golang-v1.11:latest
${REGISTRY_MIRROR}/openwhisk/action-php-v8.2:latest
${REGISTRY_MIRROR}/andersonandrei/python3action-pyaes:latest
${REGISTRY_MIRROR}/andersonandrei/python3action-linpack:latest
${REGISTRY_MIRROR}/andersonandrei/python3action-chameleon:latest
${REGISTRY_MIRROR}/andersonandrei/python3action-matmul:latest
${REGISTRY_MIRROR}/andersonandrei/python3action-image_processing:latest
)

for image in "${images[@]}"; do
    # Extract image name (remove the part between the last / and :)
    image_name=$(echo "$image" | awk -F'/' '{print $NF}' | awk -F':' '{print $1}')
    
    echo "Starting to download image: $image_name"
    python3 ./tool/download_docker_image.py "$image" "$download_dir" 
    if [ $? -eq 0 ]; then
        echo "Image $image_name downloaded successfully"
    else
        echo "Failed to download image $image_name"
    fi
done

echo -e "\nAll images have been downloaded via iSulad. The following are the download time statistics:"

mv "$download_dir"/deathstarbench_social-network-microservices_latest        "$download_dir"/social-network-microservices:latest
mv "$download_dir"/yg397_media-microservices_latest                      "$download_dir"/movie-rent-service:latest
mv "$download_dir"/deathstarbench_hotel-reservation_latest              "$download_dir"/hotel-reservation:latest
mv "$download_dir"/openwhisk_python3action_latest                       "$download_dir"/python3:latest
mv "$download_dir"/openwhisk_action-nodejs-v10_latest                  "$download_dir"/nodejs:latest
mv "$download_dir"/openwhisk_actionloop-golang-v1.11_latest      "$download_dir"/golang:latest
mv "$download_dir"/openwhisk_action-php-v8.2_latest                     "$download_dir"/php:latest
mv "$download_dir"/andersonandrei_python3action-pyaes_latest            "$download_dir"/pyaes:latest
mv "$download_dir"/andersonandrei_python3action-linpack_latest          "$download_dir"/linpack:latest
mv "$download_dir"/andersonandrei_python3action-chameleon_latest       "$download_dir"/chameleon:latest
mv "$download_dir"/andersonandrei_python3action-matmul_latest         "$download_dir"/matmul:latest
mv "$download_dir"/andersonandrei_python3action-image_processing_latest "$download_dir"/image-processing:latest

