#!/usr/bin/env python3
"""Postprocess derivation-name test output.

Normalizes all numbers in the .err file to <number>, since line numbers
in derivation.nix change whenever its documentation comments are updated.
"""
import re
import sys

err_file = sys.argv[1] + ".err"

with open(err_file, "r") as f:
    content = f.read()

# Match sequences of digits and spaces that form column-style numbers (8+ chars)
content = re.sub(r"[0-9 ]{8}(?=[^0-9])", "<number>", content)
# Match remaining bare integers
content = re.sub(r"[0-9]+", "<number>", content)

with open(err_file, "w") as f:
    f.write(content)
