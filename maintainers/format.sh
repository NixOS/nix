#!/bin/sh

find . -type f -name '*.cc' -o -name '*.hh' | xargs --max-args=8 --max-procs "$(nproc)" clang-format -i
