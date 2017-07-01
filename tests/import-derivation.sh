export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

if nix-instantiate --readonly-mode ./import-derivation.nix; then
    echo "read-only evaluation of an imported derivation unexpectedly failed"
    exit 1
fi

outPath=$(nix-build $NIX_TEST_ROOT/import-derivation.nix --no-out-link)

[ "$(cat $outPath)" = FOO579 ]
