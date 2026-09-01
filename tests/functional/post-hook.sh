#!/usr/bin/env bash

source common.sh

TODO_NixOS

export REMOTE_STORE="file://$cacheDir"
echo 'require-sigs = false' >> "$test_nix_conf"

restartDaemon

if isDaemonNewer "2.13"; then
    pushToStore="$PWD/push-to-store.sh"
else
    pushToStore="$PWD/push-to-store-old.sh"
fi

# Build the dependencies and push them to the remote store.
nix-build -o "$TEST_ROOT"/result dependencies.nix --post-build-hook "$pushToStore"
# See if all outputs are passed to the post-build hook by only specifying one
# We're not able to test CA tests this way
#
# FIXME: This export is hiding error condition
# shellcheck disable=SC2155
export BUILD_HOOK_ONLY_OUT_PATHS=$([ ! "$NIX_TESTS_CA_BY_DEFAULT" ])
nix-build -o "$TEST_ROOT"/result-mult multiple-outputs.nix -A a.first --post-build-hook "$pushToStore"

if isDaemonNewer "2.33.0pre20251029"; then
    # Regression test for issue #14287: `--check` should re-run post build
    # hook, even though nothing is getting newly registered.
    export HOOK_DEST=$TEST_ROOT/listing
    # Needed so the hook will get the above environment variable.
    restartDaemon
    nix-build -o "$TEST_ROOT"/result-mult multiple-outputs.nix --check -A a.first --post-build-hook "$PWD/build-hook-list-paths.sh"
    grepQuiet a-first "$HOOK_DEST"
    grepQuiet a-second "$HOOK_DEST"
    unset HOOK_DEST
fi

if isDaemonNewer "2.36.0pre20260901"; then
    export HOOK_DEST=$TEST_ROOT/hook-dest
    mkdir -p "$HOOK_DEST"
    restartDaemon
    nix-build -o "$TEST_ROOT"/result-mult multiple-outputs.nix --check -A a.first --post-build-hook "$PWD/build-hook-dump-drv.sh"
    grepQuiet '^Derive(' "$HOOK_DEST/drv.aterm"
    [[ $(jq -r '.outputs | keys | join(" ")' "$HOOK_DEST/build-info.json") = "first second" ]]
    unset HOOK_DEST
fi

clearStore

# Ensure that the remote store contains both the runtime and build-time
# closure of what we've just built.
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix input1_drv
nix copy --from "$REMOTE_STORE" --no-require-sigs -f multiple-outputs.nix a^second

if isDaemonNewer "2.36.0pre20260901"; then
    source common/characterisation/framework.sh
    badDiff=0
    badExitCode=0

    export HOOK_DEST=$TEST_ROOT/hook-dest
    mkdir -p "$HOOK_DEST"
    restartDaemon
    # shellcheck disable=SC2016
    nix-build -o "$TEST_ROOT"/result-drv -E '
        with import ./config.nix;
        mkDerivation {
            name = "post-hook-drv";
            args = [ "-c" "echo $deps > $out" ];
            deps = import ./dependencies.nix {};
        }' --post-build-hook "$PWD/build-hook-dump-drv.sh"
    unset HOOK_DEST

    if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
        variant=ia
    else
        variant=ca
    fi

    normalize() {
        sed \
            -e "s|$SHELL|SHELL|g" \
            -e "s|$coreutils|COREUTILS|g" \
            -e "s|$NIX_STORE_DIR/|/nix/store/|g" \
            -e "s|$system|SYSTEM|g" \
            -e 's|[0-9a-z]\{32\}-|HASH-|g' \
            -e 's|[0-9a-f]\{64\}|SALT|g'
    }

    normalize < "$TEST_ROOT/hook-dest/drv.aterm" > "$TEST_ROOT/drv.aterm"
    diffAndAcceptInner post-build-hook-drv-aterm "$TEST_ROOT/drv.aterm" "post-hook/drv-$variant.drv"
    normalize < "$TEST_ROOT/hook-dest/build-info.json" | jq -S . > "$TEST_ROOT/build-info.json"
    diffAndAcceptInner post-build-hook-build-info "$TEST_ROOT/build-info.json" "post-hook/build-info-$variant.json"

    # The hook can turn the ATerm back into JSON without the derivation
    # being in the store.
    nix derivation show --aterm-stdin "$(< "$TEST_ROOT/hook-dest/name")" < "$TEST_ROOT/hook-dest/drv.aterm" > "$TEST_ROOT/drv-shown.json"
    [[ $(jq -r '.derivations | keys[0]' "$TEST_ROOT/drv-shown.json") = $(basename "$(< "$TEST_ROOT/hook-dest/resolved-drv-path")") ]]
    normalize < "$TEST_ROOT/drv-shown.json" | jq -S . > "$TEST_ROOT/drv.json"
    diffAndAcceptInner post-build-hook-drv-json "$TEST_ROOT/drv.json" "post-hook/drv-$variant.json"
    characterisationTestExit
fi
