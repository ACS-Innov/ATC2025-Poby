#!/usr/bin/python3

import os
import subprocess

def unzip_file(file_path, output_path):
    out_unzip_path = output_path + "/" + os.path.basename(file_path)
    # Create output directory if it doesn't exist
    os.makedirs(output_path, exist_ok=True)
    try:
        command = ['gzip', '-dc', file_path] 
        with open(out_unzip_path, 'wb') as dest_file:
            subprocess.run(command, check=True, stdout=dest_file)
        print(f"Successfully extracted {file_path} to {out_unzip_path}")
    except subprocess.CalledProcessError as e:
        print(f"Error occurred: {e}")



def process_sha256_files(input_path, output_path):
    for dirpath, _, filenames in os.walk(input_path):
        for filename in filenames:
            if filename.startswith("sha256_"):
                input_file_path = os.path.join(dirpath, filename)
                unzip_file(input_file_path, output_path)

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description='Generate uncompressed layer blocks from container images')
    parser.add_argument('--input-path', default='/tmp/images_temp/isulad_tmpdir',
                      help='The path containing the original image layers')
    parser.add_argument('--output-path', default='./data/uncompress/',
                      help='Output path for uncompressed layer blocks')
    
    args = parser.parse_args()
    
    input_path = args.input_path
    output_path = args.output_path
    process_sha256_files(input_path, output_path)
