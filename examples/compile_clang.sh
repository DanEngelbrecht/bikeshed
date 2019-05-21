#!/usr/bin/env bash

set +e
mkdir -p ../build

OPT=-O3
#DISASSEMBLY='-S -masm=intel'
ASAN=""
CXXFLAGS="$CXXFLAGS -Wall -Weverything -pedantic -Wno-zero-as-null-pointer-constant -Wno-old-style-cast -Wno-sign-conversion -Wno-padded -Wno-unused-macros -Wno-c++98-compat -Wno-implicit-fallthrough -Wno-zero-as-null-pointer-constant -Wno-global-constructors"
ARCH=-m64

clang++ -o ../build/$1 $OPT $DISASSEMBLY $ARCH -std=c++14 $CXXFLAGS $ASAN -Isrc ../third-party/nadir/src/nadir.cpp $1.cpp -pthread
