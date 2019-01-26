#!/usr/bin/env bash

set +e

./compile_clang_debug.sh
./build/test_debug
./compile_clang.sh
./build/test
