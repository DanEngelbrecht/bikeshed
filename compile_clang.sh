#!/usr/bin/env bash

set +e
mkdir -p build

OPT=-O3
#DISASSEMBLY='-S -masm=intel'
ASAN=""
CXXFLAGS="$CXXFLAGS -Wall -Weverything -pedantic -Wno-old-style-cast -Wno-sign-conversion -Wno-padded -Wno-unused-macros -Wno-c++98-compat -Wno-implicit-fallthrough"
ARCH=-m64

clang++ -o ./build/test $OPT $DISASSEMBLY $ARCH -std=c++14 $CXXFLAGS $ASAN -Isrc src/bikeshed.cpp third-party/nadir/src/nadir_posix.cpp test/main.cpp
