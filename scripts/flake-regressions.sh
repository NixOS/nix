#! /usr/bin/env bash

set -e

echo "Nix version:"
nix --version

cd flake-regressions

status=0

flakes=$(ls -d tests/*/*/* | sort | head -n50)

echo "Running flake tests..."

for flake in $flakes; do

    # This test has a bad flake.lock that doesn't include
    # `lastModified` for its nixpkgs input. (#10612)
    if [[ $flake == tests/the-nix-way/nome/0.1.2 ]]; then
        continue
    fi

    if ! REGENERATE=0 ./eval-flake.sh $flake; then
        status=1
        echo "❌ $flake"
    else
        echo "✅ $flake"
    fi

done

exit "$status"
