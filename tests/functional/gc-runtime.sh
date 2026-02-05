#!/usr/bin/env bash

source common.sh

case $system in
    *linux*)
        ;;
    *)
        skipTest "Not running Linux";
esac

set -m # enable job control, needed for kill

programPath=$(nix-build --no-link ./gc-runtime.nix -A program)
environPath=$(nix-build --no-link ./gc-runtime.nix -A environ)
openPath=$(nix-build --no-link ./gc-runtime.nix -A open)

echo "backgrounding program..."
export environPath
"$programPath"/program "$openPath"/open &
sleep 2 # hack - wait for the program to get started
child=$!
echo PID=$child

nix-store --gc

kill -- -$child

if ! test -e "$programPath"; then
    echo "running program was garbage collected!"
    exit 1
fi

if ! test -e "$environPath"; then
    echo "file in environment variable was garbage collected!"
    exit 1
fi

if ! test -e "$openPath"; then
    echo "opened file was garbage collected!"
    exit 1
fi

exit 0
