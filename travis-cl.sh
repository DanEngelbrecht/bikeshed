#!/usr/bin/env bash

set +e

./compile_cl_debug.bat
./build/test_debug.exe
./compile_cl.bat
./build/test.exe
