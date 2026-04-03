#!/bin/sh

set -u

if [ "$#" -ge 2 ] && [ "$1" = "--serve" ] && [ "$2" = "--write" ]; then
    shift 2
    set -- --serve "$@"
fi

nix-store "$@"
status=$?

if [ "$status" -ne 0 ]; then
    echo "error: cannot add path '/nix/store/example' because it lacks a signature by a trusted key"
fi

exit "$status"
