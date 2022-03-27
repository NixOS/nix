source common.sh

needLocalStore "the sandbox only runs on the builder side, so it makes no sense to test it with the daemon"

clearStore

if ! canUseSandbox; then exit 99; fi

# Note: we need to bind-mount $SHELL into the chroot. Currently we
# only support the case where $SHELL is in the Nix store, because
# otherwise things get complicated (e.g. if it's in /bin, do we need
# /lib as well?).
if [[ ! $SHELL =~ /nix/store ]]; then exit 99; fi

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

export NIX_STORE_DIR=/my/store
export NIX_REMOTE=$TEST_ROOT/store0

outPath=$(nix-build dependencies.nix --no-out-link --sandbox-paths /nix/store)

[[ $outPath =~ /my/store/.*-dependencies ]]

nix path-info -r $outPath | grep input-2

nix store ls -R -l $outPath | grep foobar

nix store cat $outPath/foobar | grep FOOBAR

# Test --check without hash rewriting.
nix-build dependencies.nix --no-out-link --check --sandbox-paths /nix/store

# Test that sandboxed builds with --check and -K can move .check directory to store
nix-build check.nix -A nondeterministic --sandbox-paths /nix/store --no-out-link

(! nix-build check.nix -A nondeterministic --sandbox-paths /nix/store --no-out-link --check -K 2> $TEST_ROOT/log)
if grep -q 'error: renaming' $TEST_ROOT/log; then false; fi
grep -q 'may not be deterministic' $TEST_ROOT/log
