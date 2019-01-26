#!/usr/bin/env bash

set +e

sh ./compile_clang_debug.sh
./build/test_debug
sh ./compile_clang.sh
./build/test
