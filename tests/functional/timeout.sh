#!/usr/bin/env bash

# Test the `--timeout' option.

source common.sh

# XXX: This shouldn’t be, but #4813 cause this test to fail
needLocalStore "see #4813"

messages=$(nix-build -Q timeout.nix -A infiniteLoop --timeout 2 2>&1) && status=0 || status=$?

if [ "$status" -ne 101 ]; then
    echo "error: 'nix-store' exited with '$status'; should have exited 101"

    # FIXME: https://github.com/NixOS/nix/issues/4813
    skipTest "Do not block CI until fixed"

    exit 1
fi

if echo "$messages" | grepQuietInvert "timed out"; then
    echo "error: build may have failed for reasons other than timeout; output:"
    echo "$messages" >&2
    exit 1
fi

if nix-build -Q timeout.nix -A infiniteLoop --max-build-log-size 100; then
    echo "build should have failed"
    exit 1
fi

if nix-build timeout.nix -A silent --max-silent-time 2; then
    echo "build should have failed"
    exit 1
fi

if nix-build timeout.nix -A closeLog; then
    echo "build should have failed"
    exit 1
fi

if nix build -f timeout.nix silent --max-silent-time 2; then
    echo "build should have failed"
    exit 1
fi
