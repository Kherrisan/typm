#!/bin/bash -e


# LLVM version: 15.0.0 

ROOT=$(pwd)
# git clone git@github.com:llvm/llvm-project.git
cd $ROOT/llvm-project
git checkout e758b77161a7

if [ ! -d "build" ]; then
  mkdir build
fi

cd build

cmake -DLLVM_TARGET_ARCH="X86" \
			-DLLVM_TARGETS_TO_BUILD="X86" \
			-DCMAKE_BUILD_TYPE=Release \
			-DLLVM_ENABLE_PROJECTS="clang;lldb" \
			-DLLVM_ENABLE_RUNTIMES="compiler-rt" \
			-G "Ninja" \
			../llvm

cmake --build .

if [ ! -d "$ROOT/llvm-project/install" ]; then
  mkdir $ROOT/llvm-project/install
fi

cmake -DCMAKE_INSTALL_PREFIX=$ROOT/llvm-project/install -P cmake_install.cmake
