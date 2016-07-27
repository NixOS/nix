source common.sh

clearStore

path=$(nix-build dependencies.nix -o $TEST_ROOT/result)

deps="$(nix-store -qR $TEST_ROOT/result)"

nix-store --dump-db > $TEST_ROOT/dump

rm -rf $NIX_STATE_DIR/db

nix-store --load-db < $TEST_ROOT/dump

deps2="$(nix-store -qR $TEST_ROOT/result)"

[ "$deps" = "$deps2" ];

nix-store --dump-db > $TEST_ROOT/dump2
cmp $TEST_ROOT/dump $TEST_ROOT/dump2
