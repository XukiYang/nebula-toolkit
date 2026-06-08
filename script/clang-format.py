#!/usr/bin/env python3
import os
import sys
import subprocess
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

CPP_EXTENSIONS = {'.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.hh', '.hxx', '.ino'}

def find_cpp_files(root_dir):
    cpp_files = []
    root_path = Path(root_dir).resolve()
    for file_path in root_path.rglob('*'):
        if file_path.is_file() and file_path.suffix.lower() in CPP_EXTENSIONS:
            cpp_files.append(str(file_path))
    return cpp_files

def needs_formatting(file_path, clang_format):
    try:
        result = subprocess.run(
            [clang_format, '--style=file', file_path],
            capture_output=True,
            text=True,
            check=True
        )
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            original = f.read()
        return original != result.stdout
    except Exception:
        return False

def format_file(file_path, clang_format, dry_run):
    """
    格式化单个文件
    返回: (是否需要格式化, 文件路径)
    """
    try:
        if dry_run:
            need_format = needs_formatting(file_path, clang_format)
            return need_format, file_path
        else:
            subprocess.run(
                [clang_format, '-i', '--style=file', file_path],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            return needs_formatting(file_path, clang_format), file_path
    except subprocess.CalledProcessError:
        return False, file_path
    except Exception:
        return False, file_path

def main():
    try:
        subprocess.run(['clang-format', '--version'], capture_output=True, text=True, check=True)
    except Exception:
        print("错误: clang-format 不可用", file=sys.stderr)
        sys.exit(1)

    print("请输入要格式化的目录路径（直接回车使用当前目录）:")
    user_input = input().strip()
    
    if not user_input:
        root_dir = os.getcwd()
    else:
        root_dir = user_input

    if not os.path.isdir(root_dir):
        print(f"错误: 路径 '{root_dir}' 不是有效目录", file=sys.stderr)
        sys.exit(1)

    print(f"正在扫描目录: {root_dir}")
    cpp_files = find_cpp_files(root_dir)
    total_files = len(cpp_files)

    if total_files == 0:
        print("未找到任何 C/C++ 文件")
        return

    print(f"找到 {total_files} 个 C/C++ 文件")
    
    print("\n选择操作模式:")
    print("1. 直接格式化 (输入 'f')")
    print("2. 仅显示需要格式化的文件 (输入 'd')")
    choice = input("请选择 (f/d, 默认为 f): ").strip().lower()
    
    dry_run = choice == 'd'

    if dry_run:
        changed_files = []
        print("正在检查文件格式...")
        with ThreadPoolExecutor(max_workers=4) as executor:
            future_to_file = {
                executor.submit(format_file, f, 'clang-format', dry_run): f
                for f in cpp_files
            }

            for future in as_completed(future_to_file):
                need_format, file_path = future.result()
                if need_format:
                    changed_files.append(file_path)

        if changed_files:
            print(f"\n以下 {len(changed_files)} 个文件需要格式化:")
            for file_path in sorted(changed_files):
                print(f"  {file_path}")
        else:
            print("\n所有文件已符合格式规范")
    else:
        print("正在格式化文件...")
        with ThreadPoolExecutor(max_workers=4) as executor:
            list(executor.map(
                lambda f: format_file(f, 'clang-format', False),
                cpp_files
            ))
        print(f"\n已格式化 {total_files} 个文件")

if __name__ == '__main__':
    main()