#!/usr/bin/env bash

source common.sh
# This test is run by `tests/functional/nested-sandboxing/runner.nix` in an extra layer of sandboxing.
[[ -d /nix/store ]] || skipTest "running this test without Nix's deps being drawn from /nix/store is not yet supported"

TODO_NixOS

requireSandboxSupport
requiresUnprivilegedUserNamespaces

start="$TEST_ROOT/start"
mkdir -p "$start"
cp -r common common.sh "${config_nix}" ./nested-sandboxing "$start"
cp "${_NIX_TEST_BUILD_DIR}/common/subst-vars.sh" "$start/common"
# N.B. redefine
_NIX_TEST_SOURCE_DIR="$start"
_NIX_TEST_BUILD_DIR="$start"
cd "$start"

source ./nested-sandboxing/command.sh

# shellcheck disable=SC2016
expectStderr 100 runNixBuild badStoreUrl 2 | grepQuiet '`sandbox-build-dir` must not contain'

runNixBuild goodStoreUrl 5
