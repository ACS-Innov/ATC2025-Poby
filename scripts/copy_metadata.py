#!/usr/bin/env python3
import os
import shutil
import argparse
from pathlib import Path

def copy_metadata_files(source_dir: Path, target_dir: Path):
    # 确保目标目录存在
    target_dir.mkdir(parents=True, exist_ok=True)
    
    # 递归遍历源目录
    for dirpath, dirnames, filenames in os.walk(source_dir):
        # 获取相对路径
        rel_path = os.path.relpath(dirpath, source_dir)
        if rel_path == '.':  # 跳过根目录
            continue
            
        # 创建对应的目标文件夹
        target_folder = target_dir / rel_path
        target_folder.mkdir(parents=True, exist_ok=True)
        
        # 复制config文件
        config_file = Path(dirpath) / "config"
        if config_file.exists():
            shutil.copy2(config_file, target_folder / "config")
            print(f"Copied config from {rel_path}")
            
        # 复制manifest.json文件
        manifest_file = Path(dirpath) / "manifest.json"
        if manifest_file.exists():
            shutil.copy2(manifest_file, target_folder / "manifest.json")
            print(f"Copied manifest.json from {rel_path}")

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