source config.sh

clearStore

BUILT_STORE_PATHS=$(nix build -f ./dependencies.nix input1_drv input2_drv --no-link --print-out-paths | sort)

# Make sure that we require the `--format` argument.
expect 1 nix store export --recursive $BUILT_STORE_PATHS > "$TEST_ROOT/store-export" 2> /dev/null || \
    fail "nix store export should require the --format argument"
nix store export --format binary --recursive $BUILT_STORE_PATHS > "$TEST_ROOT/store-export"

clearStore
IMPORTED_STORE_PATHS=$(nix store import < "$TEST_ROOT/store-export" | sort)

# Make sure that the paths we built previously are still valid.
for BUILT_STORE_PATH in $BUILT_STORE_PATHS; do
    nix path-info "$BUILT_STORE_PATH" || \
        fail "path $BUILT_STORE_PATH should have been imported but isn't valid"
done
# Make sure that all the imported paths are valid.
for IMPORTED_STORE_PATH in $IMPORTED_STORE_PATHS; do
    nix path-info "$IMPORTED_STORE_PATH" ||
        fail "path $BUILT_STORE_PATH should have been imported but isn't valid"
done
