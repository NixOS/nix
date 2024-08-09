#!/usr/bin/env python3

"""
This script is a helper for this project's Meson buildsystem, to replace its
usage of `nix eval --write-to`. Writing a JSON object as a nested directory
tree is more generic, easier to maintain, and far, far less cursed. Nix
has 'good' support for JSON output. Let's just use it.
"""

import argparse
from pathlib import Path
import json
import sys

name = 'json-to-tree.py'

def log(*args, **kwargs):
    kwargs['file'] = sys.stderr
    return print(f'{name}:', *args, **kwargs)

def write_dict_to_directory(current_directory: Path, data: dict, files_written=0):
    current_directory.mkdir(parents=True, exist_ok=True)
    for key, value in data.items():
        nested_path = current_directory / key
        match value:
            case dict(nested_data):
                files_written += write_dict_to_directory(nested_path, nested_data)

            case str(content):
                nested_path.write_text(content)
                files_written += 1

            case rest:
                assert False, \
                    f'should have been called on a dict or string, not {type(rest)=}\n\t{rest=}'

    return files_written

def main():
    parser = argparse.ArgumentParser(name)
    parser.add_argument('-i', '--input', type=argparse.FileType('r'), default='-',
        help='The JSON input to operate on and output as a directory tree',
    )
    parser.add_argument('-o', '--output', type=Path, required=True,
        help='The place to put the directory tree',
    )
    args = parser.parse_args()

    json_string = args.input.read()

    try:
        data = json.loads(json_string)
    except json.JSONDecodeError:
        log(f'could not decode JSON from input: {json_string}')
        raise


    files_written = write_dict_to_directory(args.output, data)
    log(f'wrote {files_written} files')

sys.exit(main())
