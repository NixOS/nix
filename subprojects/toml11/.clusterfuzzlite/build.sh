#!/bin/bash -eu
# Copy fuzzer executable to $OUT/
$CXX $CFLAGS $LIB_FUZZING_ENGINE \
  $SRC/toml11/.clusterfuzzlite/parse_fuzzer.cpp \
  -o $OUT/parse_fuzzer \
  -I$SRC/toml11/
