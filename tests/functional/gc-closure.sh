#!/usr/bin/env bash

source common.sh

TODO_NixOS

nix_gc_closure() {
    ensureNoDeleteReferrer="${1}"
    extraArg="${2:-""}"
    clearStore
    nix build -f dependencies2.nix input0_drv --out-link "$TEST_ROOT/gc-root"
    input0=$(realpath "$TEST_ROOT/gc-root")
    input1=$(nix build -f dependencies2.nix input1_drv --no-link --print-out-paths)
    input2=$(nix build -f dependencies2.nix input2_drv --no-link --print-out-paths)
    input2_out=$(printf "%s" "$input2" | head -n1)
    input2_out2=$(printf "%s" "$input2" | tail -n1)
    top=$(nix build -f dependencies2.nix --no-link --print-out-paths)
    somthing_else=$(nix store add-path ./dependencies2.nix)

    if isDaemonNewer "2.35pre"; then
        if [[ "$extraArg" != "--delete-dead-referrers" ]] && ! "$ensureNoDeleteReferrer"; then
            nix store delete "$input2_out2"
        fi
        # Check that nix store delete --recursive --skip-alive is best-effort (doesn't fail when some paths in the closure are alive)
        # shellcheck disable=SC2086 # we want $extraArg to expand to nothing if unset
        nix store delete --recursive --skip-alive $extraArg "$top"
        [[ ! -e "$top" ]] || fail "top should have been deleted"
        [[ -e "$input0" ]] || fail "input0 is a gc root, shouldn't have been deleted"
        [[ -e "$input1" ]] || fail "input1 is not in the closure of top, it shouldn't have been deleted"
        [[ -e "$somthing_else" ]] || fail "somthing_else is not in the closure of top, it shouldn't have been deleted"
        if [[ "$extraArg" = "--delete-dead-referrers" ]]; then
            [[ ! -e "$input2_out" ]] || fail "input2_out is part of top's closure and we can delete dead referrers, it should have been deleted"
        elif "$ensureNoDeleteReferrer"; then
            [[ -e "$input2_out" ]] || fail "input2_out is part of top's closure but we can't delete dead referrers, it shouldn't have been deleted"
        else
            [[ ! -e "$input2_out" ]] || fail "input2_out is not a gc root, is part of top's closure, and has no referrers, it should have been deleted"
        fi
    elif [[ "$extraArg" = "--delete-dead-referrers" ]]; then
        expectStderr 1 nix store delete --recursive --skip-alive --delete-dead-referrers "$top" | grepQuiet "Your daemon version is too old to support deleting dead referrers."
    else
        expectStderr 1 nix store delete --recursive --skip-alive "$top" | grepQuiet "Your daemon version is too old to support garbage collecting a specific set of paths"
    fi
}

nix_gc_closure false
nix_gc_closure true
nix_gc_closure false --delete-dead-referrers