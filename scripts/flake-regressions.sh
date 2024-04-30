#! /usr/bin/env bash

set -e

echo "Nix version:"
nix --version

cd flake-regressions

status=0

flakes=$(ls -d tests/*/*/* | head -n50)

echo "Running flake tests..."

for flake in $flakes; do

    if ! REGENERATE=0 ./eval-flake.sh $flake; then
        status=1
        echo "❌ $flake"
    else
        echo "✅ $flake"
    fi

done

exit "$status"
