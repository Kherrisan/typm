#!/usr/bin/env python3

import subprocess
import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Usage: python lll.py <root_dir> <path_to_executable>")
        sys.exit(1)

    import argparse

    parser = argparse.ArgumentParser(description="Process root directory and executable path.")
    parser.add_argument("-r", "--root_dir", type=str, help="Root directory path")
    parser.add_argument("executable_path", type=str, help="Path to the executable")
    parser.add_argument("-f", "--shell_format", action="store_true", help="Add \ when breaking lines for shell")
    parser.add_argument("-o", "--output_file", type=str, help="Output file")
    parser.add_argument("-c", "--check", action="store_true", help="Check if all the .mlta.ll files exist")
    args = parser.parse_args()

    root_dir = args.root_dir
    executable_path = args.executable_path
    
    # Get the absolute path of root_dir
    root_dir = os.path.abspath(root_dir)
    
    src_files = set()
    dump_output = subprocess.check_output(["llvm-dwarfdump", "--show-sources", executable_path]).decode('utf-8')
    for line in dump_output.splitlines():
        if not line.startswith(root_dir) or line.endswith('.h'):
            continue
        if not os.path.exists(line + ".mlta.ll"):
            if args.check:
                print(line + ".mlta.ll not found", file=sys.stderr)
                sys.exit(1)
        else:
            src_files.add(line)
    
    output = ""
    for src_file in src_files:
        if args.shell_format:
            output += src_file + ".mlta.ll \\ \n"
        else:
            output += src_file + ".mlta.ll\n"
    if args.shell_format:
        output = output[:-4]
    
    if args.output_file:
        with open(args.output_file, "w") as f:
            f.write(output)
    else:
        print(output)
    

if __name__ == '__main__':
    main()

    