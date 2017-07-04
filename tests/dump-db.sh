export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

path=$(nix-build $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result)

deps="$(nix-store -qR $TEST_ROOT/result)"

nix-store --dump-db > $TEST_ROOT/dump

rm -rf $NIX_STATE_DIR/db

nix-store --load-db < $TEST_ROOT/dump

deps2="$(nix-store -qR $TEST_ROOT/result)"

[ "$deps" = "$deps2" ];

nix-store --dump-db > $TEST_ROOT/dump2
cmp $TEST_ROOT/dump $TEST_ROOT/dump2
