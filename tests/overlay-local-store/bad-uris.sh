source common.sh

requireEnvironment
setupConfig
storeDirs

mkdir -p $TEST_ROOT/bad_test
badTestRoot=$TEST_ROOT/bad_test
storeBadRoot="local-overlay?root=$badTestRoot&lower-store=$storeA&upper-layer=$storeBTop"
storeBadLower="local-overlay?root=$storeVolume/merged-store&lower-store=$badTestRoot&upper-layer=$storeBTop"
storeBadUpper="local-overlay?root=$storeVolume/merged-store&lower-store=$storeA&upper-layer=$badTestRoot"

declare -a storesBad=(
    "$storeBadRoot" "$storeBadLower" "$storeBadUpper"
)

for i in "${storesBad[@]}"; do
    echo $i
    unshare --mount --map-root-user bash <<EOF
        source common.sh
        storeDirs
        mountOverlayfs
        expectStderr 1 nix doctor --store "$i" | grepQuiet "overlay filesystem .* mounted incorrectly"
EOF
done
