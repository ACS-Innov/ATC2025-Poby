#!/bin/bash

source ./scripts/set_env.sh

# Define log directory
log_dir="log/e2e/containerd"

# Create log directory if it does not exist
if [ ! -d "$log_dir" ]; then
    mkdir -p "$log_dir"
    echo "Created directory: $log_dir"
fi

# Current minute
minute=$(date +"%M")

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
    log_file="${log_dir}/${minute}_${image_name}.log"
    
    echo "Starting to download image: $image_name"
    echo "poby" | sudo -S ctr images pull --max-concurrent-downloads 5 --plain-http "$image" > "$log_file" 2>&1
    if [ $? -eq 0 ]; then
        echo "Image $image_name downloaded successfully"
    else
        echo "Failed to download image $image_name, please check the log $log_file"
    fi

    echo "poby" | sudo -S ctr images rm "$image" > /dev/null 2>&1
done

echo -e "\nAll images have been downloaded via containerd. The following are the download time statistics:"
for image in "${images[@]}"; do
    # Extract image name
    image_name=$(echo "$image" | awk -F'/' '{print $NF}' | awk -F':' '{print $1}')
    log_file="${log_dir}/${minute}_${image_name}.log"
    
    # Check if the log file exists
    if [ -f "$log_file" ]; then
        # Extract download time
        time=$(grep "done: " "$log_file" | grep -oE '([0-9.]+[smh]\.?){1,3}')
        if [ -n "$time" ]; then
            echo "$image_name: $time"
        else
            echo "$image_name: Download time not found"
        fi
    else
        echo "$image_name: Log file does not exist"
    fi
done

