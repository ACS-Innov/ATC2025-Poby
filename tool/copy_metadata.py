import os
import shutil
import argparse
from pathlib import Path

def copy_metadata_files(source_dir: Path, target_dir: Path):
    target_dir.mkdir(parents=True, exist_ok=True)
    
    for folder in source_dir.iterdir():
        if not folder.is_dir():
            continue
            
        target_folder = target_dir / folder.name
        target_folder.mkdir(exist_ok=True)
        
        config_file = folder / "config"
        if config_file.exists():
            shutil.copy2(config_file, target_folder / "config")
            
        manifest_file = folder / "manifest.json"
        if manifest_file.exists():
            shutil.copy2(manifest_file, target_folder / "manifest.json")

def main():
    parser = argparse.ArgumentParser(description='Copy config and manifest.json files from source to target directory')
    parser.add_argument('--input-path', type=str, required=True, help='Source directory containing the image folders')
    parser.add_argument('--output-path', type=str, required=True, help='Target directory to store the metadata files')
    
    args = parser.parse_args()
    
    source_dir = Path(args.input_path)
    target_dir = Path(args.output_path)
    
    if not source_dir.exists():
        print(f"Error: Source directory '{source_dir}' does not exist")
        return
    
    copy_metadata_files(source_dir, target_dir)

if __name__ == "__main__":
    main() 