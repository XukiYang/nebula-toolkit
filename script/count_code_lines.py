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
    parser = argparse.ArgumentParser(description='批量 clang-format 格式化工具')
    parser.add_argument('path', nargs='?', default='.', help='要处理的目录路径（默认为当前目录）')
    parser.add_argument('--dry-run', action='store_true', help='仅显示需要格式化的文件而不实际修改')
    parser.add_argument('--clang-format', default='clang-format', help='clang-format 可执行文件路径')
    parser.add_argument('--workers', type=int, default=4, help='并行工作线程数')
    args = parser.parse_args()

    try:
        subprocess.run([args.clang_format, '--version'], capture_output=True, text=True, check=True)
    except Exception:
        print("错误: clang-format 不可用", file=sys.stderr)
        sys.exit(1)

    root_dir = os.path.abspath(args.path)
    if not os.path.isdir(root_dir):
        print(f"错误: 路径 '{root_dir}' 不是有效目录", file=sys.stderr)
        sys.exit(1)

    cpp_files = find_cpp_files(root_dir)
    total_files = len(cpp_files)

    if total_files == 0:
        print("未找到任何 C/C++ 文件")
        return

    if args.dry_run:
        changed_files = []
        with ThreadPoolExecutor(max_workers=args.workers) as executor:
            future_to_file = {
                executor.submit(format_file, f, args.clang_format, args.dry_run): f
                for f in cpp_files
            }

            for future in as_completed(future_to_file):
                need_format, file_path = future.result()
                if need_format:
                    changed_files.append(file_path)

        if changed_files:
            for file_path in sorted(changed_files):
                print(file_path)
            print(f"\n找到 {len(changed_files)} 个需要格式化的文件")
        else:
            print("所有文件已符合格式规范")
    else:
        with ThreadPoolExecutor(max_workers=args.workers) as executor:
            list(executor.map(
                lambda f: format_file(f, args.clang_format, False),
                cpp_files
            ))
        print(f"已格式化 {total_files} 个文件")

if __name__ == '__main__':
    main()