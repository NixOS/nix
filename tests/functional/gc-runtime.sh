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

nix-env -p "$profiles/test" -f ./gc-runtime.nix -i gc-runtime

outPath=$(nix-env -p "$profiles/test" -q --no-name --out-path gc-runtime)
echo "$outPath"

fifo="$TEST_ROOT/fifo"
mkfifo "$fifo"

echo "backgrounding program..."
"$profiles"/test/program "$fifo" &
child=$!
echo PID=$child
cat "$fifo"

expectStderr 1 nix-store --delete "$outPath" | grepQuiet "Cannot delete path.*because it's referenced by the GC root '/proc/"

nix-env -p "$profiles/test" -e gc-runtime
nix-env -p "$profiles/test" --delete-generations old

nix-store --gc

kill -- -$child

if ! test -e "$outPath"; then
    echo "running program was garbage collected!"
    exit 1
fi

exit 0
