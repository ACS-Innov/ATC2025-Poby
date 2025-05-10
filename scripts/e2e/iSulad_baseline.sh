#!/bin/bash

source ./scripts/set_env.sh
# Define log directory
log_dir="log/e2e/iSulad"

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
    echo $SUDO_PASSWORD | sudo -S ./build/src/test/image_ops_test ${image} > "$log_file" 2>&1
    if [ $? -eq 0 ]; then
        echo "Image $image_name downloaded successfully"
    else
        echo "Failed to download image $image_name, please check the log $log_file"
    fi

    echo $SUDO_PASSWORD | sudo -S ctr images rm "$image" > /dev/null 2>&1
done

echo -e "\nAll images have been downloaded via iSulad. The following are the download time statistics:"
for image in "${images[@]}"; do
    # Extract image name
    image_name=$(echo "$image" | awk -F'/' '{print $NF}' | awk -F':' '{print $1}')
    log_file="${log_dir}/${minute}_${image_name}.log"
    
    # Check if the log file exists
    if [ -f "$log_file" ]; then
        # Extract download time
	start_time=$(awk 'NR==3 {print $1, $2}' "$log_file" | tr -d '[]')
	end_time=$(cat ${log_file} | head -n -3 |awk 'END {print $1, $2}' | tr -d '[]')
	start_epoch=$(date -d "$start_time" "+%s.%N")
        end_epoch=$(date -d "$end_time" "+%s.%N")
	# Calculate duration in seconds
        duration=$(echo "$end_epoch - $start_epoch" | bc)
        if [ -n "$duration" ]; then
            echo "$image_name: $duration"
        else
            echo "$image_name: Download time not found"
        fi
    else
        echo "$image_name: Log file does not exist"
    fi
done

