#!/usr/bin/env bash

# https://github.com/fmtlib/fmt/issues/282
a=fmt
b=libfmt
find . -name '*.h' -exec sed -i.bak "s/${a}::/${b}::/g; s/namespace ${a}/namespace ${b}/g" '{}' \;
