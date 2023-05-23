source common.sh

clearStore

drvPath=$(nix-instantiate dependencies.nix)
outPath=$(nix-store -rvv "$drvPath")

# Set a GC root.
rm -f "$NIX_STATE_DIR"/gcroots/foo
ln -sf $outPath "$NIX_STATE_DIR"/gcroots/foo

[ "$(nix-store -q --roots $outPath)" = "$NIX_STATE_DIR/gcroots/foo -> $outPath" ]

nix-store --gc --print-roots | grep $outPath
nix-store --gc --print-live | grep $outPath
nix-store --gc --print-dead | grep $drvPath
if nix-store --gc --print-dead | grep -E $outPath$; then false; fi

nix-store --gc --print-dead

inUse=$(readLink $outPath/reference-to-input-2)
if nix-store --delete $inUse; then false; fi
test -e $inUse

if nix-store --delete $outPath; then false; fi
test -e $outPath

for i in $NIX_STORE_DIR/*; do
    if [[ $i =~ /trash ]]; then continue; fi # compat with old daemon
    touch $i.lock
    touch $i.chroot
done

nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat $outPath/foobar
cat $outPath/reference-to-input-2/bar

# Check that the derivation has been GC'd.
if test -e $drvPath; then false; fi

rm "$NIX_STATE_DIR"/gcroots/foo

nix-collect-garbage

# Check that the output has been GC'd.
if test -e $outPath/foobar; then false; fi

# Check that the store is empty.
rmdir $NIX_STORE_DIR/.links
rmdir $NIX_STORE_DIR

## Test `nix-collect-garbage -d`
testCollectGarbageD () {
    clearProfiles
    # Run two `nix-env` commands, should create two generations of
    # the profile
    nix-env -f ./user-envs.nix -i foo-1.0
    nix-env -f ./user-envs.nix -i foo-2.0pre1
    [[ $(nix-env --list-generations | wc -l) -eq 2 ]]

    # Clear the profile history. There should be only one generation
    # left
    nix-collect-garbage -d
    [[ $(nix-env --list-generations | wc -l) -eq 1 ]]
}
# `nix-env` doesn't work with CA derivations, so let's ignore that bit if we're
# using them
if [[ -z "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    testCollectGarbageD

    # Run the same test, but forcing the profiles at their legacy location under
    # /nix/var/nix.
    #
    # Regression test for #8294
    rm ~/.nix-profile
    ln -s $NIX_STATE_DIR/profiles/per-user/me ~/.nix-profile
    testCollectGarbageD
fi
