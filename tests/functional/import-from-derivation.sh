#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStoreIfPossible

export NIX_PATH=config="${config_nix}"

if nix-instantiate --readonly-mode ./import-from-derivation.nix -A result; then
    echo "read-only evaluation of an imported derivation unexpectedly failed"
    exit 1
fi

outPath=$(nix-build ./import-from-derivation.nix -A result --no-out-link)

[ "$(cat "$outPath")" = FOO579 ]

# Check that we can have access to the entire closure of a derivation output.
nix build --no-link --restrict-eval -I src=. -f ./import-from-derivation.nix importAddPathExpr -v

# FIXME: the next tests are broken on CA.
if [[ -n "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    exit 0
fi

# Test filterSource on the result of a derivation.
outPath2=$(nix-build ./import-from-derivation.nix -A addPath --no-out-link)
[[ "$(cat "$outPath2")" = BLAFOO579 ]]

# Test that IFD works with a chroot store.
if canUseSandbox; then

    store2="$TEST_ROOT/store2"
    store2_url="$store2?store=$NIX_STORE_DIR"

    # Copy the derivation outputs to the chroot store to avoid having
    # to actually build anything, as that would fail due to the lack
    # of a shell in the sandbox. We only care about testing the IFD
    # semantics.
    for i in bar result addPath; do
        nix copy --to "$store2_url" --no-check-sigs "$(nix-build ./import-from-derivation.nix -A "$i" --no-out-link)"
    done

    clearStore

    outPath_check=$(nix-build ./import-from-derivation.nix -A result --no-out-link --store "$store2_url")
    [[ "$outPath" = "$outPath_check" ]]
    [[ ! -e "$outPath" ]]
    [[ -e "$store2/nix/store/$(basename "$outPath")" ]]

    outPath2_check=$(nix-build ./import-from-derivation.nix -A addPath --no-out-link --store "$store2_url")
    [[ "$outPath2" = "$outPath2_check" ]]
fi
