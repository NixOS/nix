source common.sh

clearStore

if nix-instantiate --readonly-mode ./import-derivation.nix; then
    echo "read-only evaluation of an imported derivation unexpectedly failed"
    exit 1
fi

outPath=$(nix-build ./import-derivation.nix --no-out-link)

[ "$(cat $outPath)" = FOO579 ]
