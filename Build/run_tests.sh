#!/bin/sh
# Simple build script for running unit tests
set -e
mkdir -p build_tests
cc -IInclude tests/test_distance.c -o build_tests/test_distance
build_tests/test_distance
cc -IInclude tests/test_endian.c -o build_tests/test_endian
build_tests/test_endian
