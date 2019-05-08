#!/usr/bin/env bash

set +e
mkdir -p build

OPT=-O3
#DISASSEMBLY='-S -masm=intel'
ASAN=""
CXXFLAGS="$CXXFLAGS -Wall -Weverything -pedantic -Wno-zero-as-null-pointer-constant -Wno-old-style-cast -Wno-global-constructors -Wno-padded"
ARCH=-m64
CXXFLAGS="$CXXFLAGS -fprofile-instr-generate -fcoverage-mapping"

clang++ -o ./build/test_cov $OPT $DISASSEMBLY $ARCH -std=c++14 -DBIKESHED_ASSERTS $CXXFLAGS $ASAN -Isrc third-party/nadir/src/nadir.cpp test/test_c99.c test/test.cpp test/main.cpp -pthread

pushd build
./test_cov
xcrun llvm-profdata merge -o test_cov.profdata default.profraw
xcrun llvm-cov show ./test_cov -instr-profile=test_cov.profdata >test_cov.txt
popd
