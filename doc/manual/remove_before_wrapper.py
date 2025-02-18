#!/usr/bin/env python3

import os
import subprocess
import sys
import shutil
import typing as t

def main():
    if len(sys.argv) < 4 or '--' not in sys.argv:
        print("Usage: remove-before-wrapper <output> -- <nix command...>")
        sys.exit(1)

    # Extract the parts
    output: str = sys.argv[1]
    nix_command_idx: int = sys.argv.index('--') + 1
    nix_command: t.List[str] = sys.argv[nix_command_idx:]

    output_temp: str = output + '.tmp'

    # Remove the output and temp output in case they exist
    shutil.rmtree(output, ignore_errors=True)
    shutil.rmtree(output_temp, ignore_errors=True)

    # Execute nix command with `--write-to` tempary output
    nix_command_write_to = nix_command + ['--write-to', output_temp]
    subprocess.run(nix_command_write_to, check=True)

    # Move the temporary output to the intended location
    os.rename(output_temp, output)

if __name__ == "__main__":
    main()
