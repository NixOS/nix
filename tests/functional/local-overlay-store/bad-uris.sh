# shellcheck shell=bash
source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
setupStoreDirs

mkdir -p "$TEST_ROOT"/bad_test
badTestRoot=$TEST_ROOT/bad_test
storeBadRoot="local-overlay://?root=$badTestRoot&lower-store=$storeA&upper-layer=$storeBTop"
storeBadLower="local-overlay://?root=$storeBRoot&lower-store=$badTestRoot&upper-layer=$storeBTop"
storeBadUpper="local-overlay://?root=$storeBRoot&lower-store=$storeA&upper-layer=$badTestRoot"

declare -a storesBad=(
    "$storeBadRoot" "$storeBadLower" "$storeBadUpper"
)

TODO_NixOS

for i in "${storesBad[@]}"; do
    echo "$i"
    # shellcheck disable=SC2119
    execUnshare <<EOF
        source common.sh
        setupStoreDirs
        mountOverlayfs
        expectStderr 1 nix doctor --store "$i" | grepQuiet "overlay filesystem .* mounted incorrectly"
EOF
done
