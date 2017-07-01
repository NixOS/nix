export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

path=$(nix-build $NIX_TEST_ROOT/dependencies.nix --no-out-link)

# Test nix-store -l.
[ "$(nix-store -l $path)" = FOO ]

# Test compressed logs.
clearStore
rm -rf $NIX_LOG_DIR
(! nix-store -l $path)
nix-build $NIX_TEST_ROOT/dependencies.nix --no-out-link --option build-compress-log true
[ "$(nix-store -l $path)" = FOO ]
