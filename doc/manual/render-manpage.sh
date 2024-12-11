#!/usr/bin/env bash

set -euo pipefail

lowdown_args=

if [ "$1" = --out-no-smarty ]; then
    lowdown_args=--out-no-smarty
    shift
fi

[ "$#" = 4 ] || {
    echo "wrong number of args passed" >&2
    exit 1
}

title="$1"
section="$2"
infile="$3"
outfile="$4"

(
    printf "Title: %s\n\n" "$title"
    cat "$infile"
) | lowdown -sT man --nroff-nolinks $lowdown_args -M section="$section" -o "$outfile"
