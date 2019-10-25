#! /usr/bin/env bash

rm -rf build*
meson build-gcc

cd build-gcc
ninja -j2

