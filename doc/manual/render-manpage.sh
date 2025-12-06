#!/usr/bin/env bash
#
# Standalone manpage renderer that doesn't require mdbook.
# Uses expand-includes.py to preprocess markdown, then lowdown to generate manpages.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

lowdown_args=

# Optional --out-no-smarty flag for compatibility with nix_nested_manpages
if [ "$1" = --out-no-smarty ]; then
    lowdown_args=--out-no-smarty
    shift
fi

[ "$#" = 6 ] || {
    cat >&2 <<EOF
Usage: $0 [--out-no-smarty] <title> <section> <source-root> <generated-root> <infile> <outfile>

Arguments:
  title           - Manpage title (e.g., "nix-env --install")
  section         - Manpage section number (1, 5, 8, etc.)
  source-root     - Root directory of markdown sources
  generated-root  - Root directory of generated markdown files
  infile          - Input markdown file (relative to build directory)
  outfile         - Output manpage file

Examples:
  $0 "nix-store --query" 1 doc/manual/source build/doc/manual/source \\
     build/doc/manual/source/command-ref/nix-store/query.md nix-store-query.1
EOF
    exit 1
}

title="$1"
section="$2"
source_root="$3"
generated_root="$4"
infile="$5"
outfile="$6"

# Expand includes and pipe to lowdown
(
    printf "Title: %s\n\n" "$title"
    python3 "$script_dir/expand-includes.py" \
        --source-root "$source_root" \
        --generated-root "$generated_root" \
        "$infile"
) | lowdown -sT man --nroff-nolinks $lowdown_args -M section="$section" -o "$outfile"
