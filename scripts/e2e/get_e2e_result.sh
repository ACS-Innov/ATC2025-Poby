#!/bin/bash

convert_to_seconds() {
    local time_str=$1
    if [[ $time_str == *m* ]]; then
        local minutes=$(echo $time_str | cut -d'm' -f1)
        local seconds=$(echo $time_str | cut -d'm' -f2 | sed 's/s//')
        echo "$(bc -l <<< "${minutes} * 60 + ${seconds}")"
    else
        echo "$(echo $time_str | sed 's/s//')"
    fi
}

# Extract times from log files
declare -A tests=(
    [SN]="social-network-microservices"
    [MS]="movie-rent-service|media-microservices"
    [HR]="hotel-reservation"
    [PY]="python3 |python3action "
    [JS]="nodejs|action-nodejs-v10"
    [GO]="golang|actionloop-golang-v1.11"
    [PP]="php|action-php-v8.2"
    [PS]="pyaes|python3action-pyaes"
    [LP]="linpack|python3action-linpack"
    [CL]="chameleon|python3action-chameleon"
    [MT]="matmul|python3action-matmul"
    [ML]="image-processing|python3action-image_processing"
)

log_files=("log/e2e/poby.log" "log/e2e/iSulad.log" "log/e2e/containerd.log")
tool_names=("Poby" "iSulad" "Containerd")

declare -A data

for i in {0..2}; do
    log=${log_files[$i]}
    tool=${tool_names[$i]}
    while read -r line; do
        for key in "${!tests[@]}"; do
            if [[ $line =~ ${tests[$key]} ]]; then
                time_str=$(echo "$line" | grep -oE '[0-9]+m[0-9.]+s|[0-9.]+s')
                seconds=$(convert_to_seconds "$time_str")
                data["$tool,$key"]=$seconds
            fi
        done
    done < "$log"
done

# Define the desired order of test sets
ordered_tests=("SN" "MS" "HR" "PY" "JS" "GO" "PP" "PS" "LP" "CL" "MT" "ML")

# Generate output table
printf "%-10s %-10s %-10s %-10s |   %-15s %-15s %-15s\n" "Test Set" "Poby" "Containerd" "iSulad" "Poby/Poby" "Containerd/Poby" "iSulad/Poby"

# Loop through the ordered test sets
for key in "${ordered_tests[@]}"; do
    p=$(echo "${data[Poby,$key]}" | awk '{printf "%.3f", $1}')
    c=$(echo "${data[Containerd,$key]}" | awk '{printf "%.3f", $1}')
    i=$(echo "${data[iSulad,$key]}" | awk '{printf "%.3f", $1}')
    pr=$(awk -v a=$p -v b=$p 'BEGIN {printf "%.3f", a / b}')
    cr=$(awk -v a=$c -v b=$p 'BEGIN {printf "%.3f", a / b}')
    ir=$(awk -v a=$i -v b=$p 'BEGIN {printf "%.3f", a / b}')
    printf "%-10s %-10s %-10s %-10s |   %-15s %-15s %-15s\n" "$key" "$p" "$c" "$i" "$pr" "$cr" "$ir"
done
