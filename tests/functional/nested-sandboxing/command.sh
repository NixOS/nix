# shellcheck shell=bash
set -eu -o pipefail

NIX_BIN_DIR=$(dirname "$(type -p nix)")
export NIX_BIN_DIR
# TODO Get Nix and its closure more flexibly
EXTRA_SANDBOX="/nix/store $(dirname "$NIX_BIN_DIR")"
export EXTRA_SANDBOX

badStoreUrl () {
    local altitude=$1
    echo "$TEST_ROOT"/store-"$altitude"
}

goodStoreUrl () {
    local altitude=$1
    echo "$("badStoreUrl" "$altitude")"?store=/foo-"$altitude"
}

# The non-standard sandbox-build-dir helps ensure that we get the same behavior
# whether this test is being run in a derivation as part of the nix build or
# being manually run by a developer outside a derivation
runNixBuild () {

    local storeFun=$1
    local altitude=$2
    nix-build \
        --no-substitute --no-out-link \
        --store "$("$storeFun" "$altitude")" \
        --extra-sandbox-paths "$EXTRA_SANDBOX" \
        ./nested-sandboxing/runner.nix \
        --arg altitude "$((altitude - 1))" \
        --argstr storeFun "$storeFun" \
        --sandbox-build-dir /build-non-standard
}
