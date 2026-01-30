#!/usr/bin/env bash

source common.sh

case $system in
    *linux*)
        ;;
    *)
        skipTest "Not running Linux";
esac

set -m # enable job control, needed for kill

TODO_NixOS

profiles="$NIX_STATE_DIR"/profiles
rm -rf "$profiles"

nix-env -p "$profiles/test" -f ./gc-runtime.nix -i gc-runtime-{program,environ,open}

programPath=$(nix-env -p "$profiles/test" -q --no-name --out-path gc-runtime-program)
environPath=$(nix-env -p "$profiles/test" -q --no-name --out-path gc-runtime-environ)
openPath=$(nix-env -p "$profiles/test" -q --no-name --out-path gc-runtime-open)

echo "backgrounding program..."
export environPath
"$profiles"/test/program "$openPath"/open &
sleep 2 # hack - wait for the program to get started
child=$!
echo PID=$child

nix-env -p "$profiles/test" -e gc-runtime-{program,environ,open}
nix-env -p "$profiles/test" --delete-generations old

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
