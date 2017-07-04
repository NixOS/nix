export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

if [[ $(uname) != Linux ]]; then exit; fi

set -m # enable job control, needed for kill

profiles="$NIX_STATE_DIR"/profiles
rm -rf $profiles

nix-env -p $profiles/test -f "$NIX_TEST_ROOT/gc-runtime.nix" -i gc-runtime

outPath=$(nix-env -p $profiles/test -q --no-name --out-path gc-runtime)
echo $outPath

echo "backgrounding program..."
$profiles/test/program &
sleep 2 # hack - wait for the program to get started
child=$!
echo PID=$child

nix-env -p $profiles/test -e gc-runtime
nix-env -p $profiles/test --delete-generations old

nix-store --gc

kill -- -$child

if ! test -e $outPath; then
    echo "running program was garbage collected!"
    exit 1
fi

exit 0
