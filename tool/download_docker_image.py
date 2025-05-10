import requests
import os
import sys
import json

def download_file(url, filename):
    """Download a file from the given URL and save it locally."""
    response = requests.get(url, stream=True)
    response.raise_for_status()
    with open(filename, "wb") as file:
        for chunk in response.iter_content(1024):
            file.write(chunk)
    print(f"Downloaded: {filename}")

def get_manifest(registry, repo_name, tag):
    """Fetch the manifest of the specified image from the registry."""
    url = f"{registry}/v2/{repo_name}/manifests/{tag}"
    headers = {"Accept": "application/vnd.docker.distribution.manifest.v2+json"}
    response = requests.get(url, headers=headers)
    response.raise_for_status()
    return response.json()

def download_blob(registry, repo_name, digest, output_dir, filename):
    """Download a blob (config or layer) from the registry."""
    url = f"{registry}/v2/{repo_name}/blobs/{digest}"
    download_file(url, os.path.join(output_dir, filename))

def main(image, base_dir):
    """Main function to download manifest, config, and layers."""
    if "/" not in image or ":" not in image:
        print("Please provide image in '<registry>/<name>:<tag>' format.")
        sys.exit(1)

    # Split registry, image name, and tag
    registry, image = image.split("/", 1)
    repo_name, tag = image.split(":")
    registry = f"http://{registry}"  # Use HTTP protocol

    # Construct the output directory based on the provided base_dir
    output_dir = os.path.join(base_dir, f"{repo_name.replace('/', '_')}_{tag}")

    # Create the output directory if it does not exist
    os.makedirs(output_dir, exist_ok=True)

    # Get the manifest and save it
    manifest = get_manifest(registry, repo_name, tag)
    with open(os.path.join(output_dir, "manifest.json"), "w") as file:
        json.dump(manifest, file, indent=4)
    print("Manifest saved.")

    # Download the config file and name it "config"
    config_digest = manifest["config"]["digest"]
    download_blob(registry, repo_name, config_digest, output_dir, "config")

    # Download all layers
    layers = manifest.get("layers", [])
    for layer in layers:
        digest = layer["digest"]
        filename = digest.replace(":", "_")
        download_blob(registry, repo_name, digest, output_dir, filename)

    print(f"All files downloaded to '{output_dir}'.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 download_docker_image.py <registry>/<image>:<tag> <base_dir>")
    else:
        image = sys.argv[1]
        base_dir = sys.argv[2]
        main(image, base_dir)

