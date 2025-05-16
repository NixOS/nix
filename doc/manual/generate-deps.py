#!/usr/bin/env python3

import glob
import sys

# meson expects makefile-style dependency declarations, i.e.
#
#   target: dependency...
#
# meson seems to pass depfiles straight on to ninja even though
# it also parses the file itself (or at least has code to do so
# in its tree), so we must live by ninja's rules: only slashes,
# spaces and octothorpes can be escaped, anything else is taken
# literally. since the rules for these aren't even the same for
# all three we will just fail when we encounter any of them (if
# asserts are off for some reason the depfile will likely point
# to nonexistent paths, making everything phony and thus fine.)
for path in glob.glob(sys.argv[1] + '/**', recursive=True):
    assert '\\' not in path
    assert ' ' not in path
    assert '#' not in path
    print("ignored:", path)
