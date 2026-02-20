#!/usr/bin/env python3
"""Enumerate lang test names for meson. Prints one name per line: subdir/stem"""
import sys
from pathlib import Path

source_dir = Path(sys.argv[1])
for subdir in ["eval-fail", "eval-okay", "parse-fail", "parse-okay"]:
    dir_path = source_dir / subdir
    if dir_path.is_dir():
        for nix_file in sorted(dir_path.glob("*.nix")):
            print(f"{subdir}/{nix_file.stem}")
