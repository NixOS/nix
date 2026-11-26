#!/usr/bin/env bash

source common.sh

requireSandboxSupport
requiresUnprivilegedUserNamespaces
[[ "${busybox-}" =~ busybox ]] || skipTest "no busybox"

enableFeatures "ca-derivations"
unset NIX_STORE_DIR

file=build-remote-content-addressed-cache-loss.nix

workerStates=("${CACHE_LOSS_WORKER_STATE:-cached}")
for workerState in "${workerStates[@]}"; do
    client="$TEST_ROOT/$workerState/machine0"
    worker="$TEST_ROOT/$workerState/machine1"
    builder="ssh-ng://localhost?remote-store=$worker?system-features=cache-loss%20ca-derivations - - 1 1 cache-loss,ca-derivations"
    result="$TEST_ROOT/$workerState/result"
    mkdir -p "$TEST_ROOT/$workerState"

    nix-instantiate "$file" -A b --arg busybox "$busybox" \
        --store "$client" --add-root "$TEST_ROOT/$workerState/recipe"
    bDrv=$(readlink "$TEST_ROOT/$workerState/recipe")
    aDrv=$(nix-instantiate "$file" -A a --arg busybox "$busybox" --store "$client")
    nix build -L -f "$file" b --arg busybox "$busybox" \
        --store "$client" --max-jobs 0 --builders "$builder" -o "$result"
    aPath=$(nix store build-trace info --store "$client" "$aDrv^out" --json \
        | jq -r '.[] | .opaquePath | select(.)')
    nix-store --store "$worker" --check-validity "$aPath"
    test -e "$client/$aPath"

    rm "$result"
    nix-store --store "$client" --gc --print-dead \
        --option keep-derivations true --option keep-outputs false | grep -Fx "$aPath"
    nix-store --store "$client" --gc --option keep-derivations true --option keep-outputs false
    nix-store --store "$client" --check-validity "$bDrv" "$aDrv"
    test ! -e "$client/$aPath"
    expect 1 nix-store --store "$client" --check-validity "$aPath"
    nix store build-trace info --store "$client" "$aDrv^out" --json \
        | jq -e --arg path "$aPath" \
            'any(.[]; .key != null and .value.outPath == ($path | split("/")[-1]))'

    if [[ "$workerState" == fresh ]]; then
        worker="$TEST_ROOT/$workerState/machine2"
        test ! -e "$worker"
        builder="ssh-ng://localhost?remote-store=$worker?system-features=cache-loss%20ca-derivations - - 1 1 cache-loss,ca-derivations"
    else
        nix-store --store "$worker" --check-validity "$aPath"
    fi

    nix build -L -f "$file" c --arg busybox "$busybox" \
        --store "$client" --max-jobs 0 --builders "$builder" -o "$result" \
        2>&1 | tee "$TEST_ROOT/$workerState/rebuild.log"
    nix-store --store "$client" --check-validity "$aPath"
    test -e "$client/$aPath"
    grep -Fx 'input second' "$client/$(readlink "$result")"
    if [[ "$workerState" == fresh ]]; then
        grep 'CACHE-LOSS-A-BUILT' "$TEST_ROOT/$workerState/rebuild.log"
    fi
done
