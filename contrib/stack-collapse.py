#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p python3 --pure

# To be used with `--trace-function-calls` and `flamegraph.pl`.
#
# For example:
#
# nix-instantiate --trace-function-calls '<nixpkgs>' -A hello 2> nix-function-calls.trace
# ./contrib/stack-collapse.py nix-function-calls.trace > nix-function-calls.folded
# nix-shell -p flamegraph --run "flamegraph.pl nix-function-calls.folded > nix-function-calls.svg"

import sys
from pprint import pprint
import fileinput

stack = []
timestack = []

#Reads the input file line by line
for line in fileinput.input():
    
    #Split the line read from the file, by ' ' character into two
    components = line.strip().split(" ", 2)

    #Read next line if first word on splitting is 'function-trace'
    if components[0] != "function-trace":
        continue

    direction = components[1]
    components = components[2].rsplit(" ", 2)

    loc = components[0]
    _at = components[1]
    time = int(components[2])

    #Verify if direction is 'entered' Or 'exited'
    if direction == "entered":
        stack.append(loc)
        timestack.append(time)
    elif direction == "exited":
        dur = time - timestack.pop()
        vst = ";".join(stack)
        print(f"{vst} {dur}")
        stack.pop()
