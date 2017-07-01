export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

if [[ $(uname) != Linux ]]; then exit; fi
if [[ ! $SHELL =~ /nix/store ]]; then exit; fi

chmod -R u+w $TEST_ROOT/store0 || true
chmod -R u+w $TEST_ROOT/store1 || true
rm -rf $TEST_ROOT/store0 $TEST_ROOT/store1

# FIXME: --option is not passed to build-remote, so have to create a config file.
export NIX_CONF_DIR=$TEST_ROOT/etc2
mkdir -p $NIX_CONF_DIR
echo "
build-sandbox-paths = /nix/store
sandbox-build-dir = /build-tmp
" > $NIX_CONF_DIR/nix.conf

outPath=$(nix-build $NIX_TEST_ROOT/build-hook.nix --no-out-link -j0 \
  --option builders "local?root=$TEST_ROOT/store0; local?root=$TEST_ROOT/store1 - - 1 1 foo")

cat $outPath/foobar | grep FOOBAR

# Ensure that input1 was built on store1 due to the required feature.
p=$(readlink -f $outPath/input-2)
(! nix path-info --store local?root=$TEST_ROOT/store0 --all | grep dependencies.builder1.sh)
nix path-info --store local?root=$TEST_ROOT/store1 --all | grep dependencies.builder1.sh
