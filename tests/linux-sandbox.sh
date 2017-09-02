source common.sh

clearStore

if [[ $(uname) != Linux ]]; then exit; fi

# Note: we need to bind-mount $SHELL into the chroot. Currently we
# only support the case where $SHELL is in the Nix store, because
# otherwise things get complicated (e.g. if it's in /bin, do we need
# /lib as well?).
if [[ ! $SHELL =~ /nix/store ]]; then exit; fi

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

export NIX_STORE_DIR=/my/store
export NIX_REMOTE="local?root=$TEST_ROOT/store0"

outPath=$(nix-build dependencies.nix --no-out-link --option sandbox-paths /nix/store)

[[ $outPath =~ /my/store/.*-dependencies ]]

nix path-info -r $outPath | grep input-2

nix ls-store -R -l $outPath | grep foobar

nix cat-store $outPath/foobar | grep FOOBAR
