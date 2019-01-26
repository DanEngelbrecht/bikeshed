#!/usr/bin/env bash

set +e
mkdir -p build

#OPT=-O3
OPT="-g -O1"
#DISASSEMBLY='-S -masm=intel'
ASAN="-fsanitize=address -fno-omit-frame-pointer"
CXXFLAGS="$CXXFLAGS -Wall -Weverything -pedantic -Wno-old-style-cast -Wno-sign-conversion -Wno-padded -Wno-unused-macros -Wno-c++98-compat -Wno-implicit-fallthrough"
ARCH=-m64

clang++ -o ./build/test_debug $OPT $DISASSEMBLY $ARCH -std=c++14 $CXXFLAGS $ASAN -Isrc src/sogo.cpp src/sogo_nodes.cpp src/sogo_utils.cpp third-party/xxhash-1/xxhash.c test/main.cpp
