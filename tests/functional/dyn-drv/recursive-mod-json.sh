# shellcheck shell=bash
source common.sh

# FIXME
if [[ $(uname) != Linux ]]; then skipTest "Not running Linux"; fi

export NIX_TESTS_CA_BY_DEFAULT=1

enableFeatures 'recursive-nix'
restartDaemon

clearStore

rm -f "$TEST_ROOT"/result

EXTRA_PATH=$(dirname "$(type -p nix)"):$(dirname "$(type -p jq)")
export EXTRA_PATH

# Will produce a drv
metaDrv=$(nix-instantiate ./recursive-mod-json.nix)

# computed "dynamic" derivation
drv=$(nix-store -r "$metaDrv")

# build that dyn drv
res=$(nix-store -r "$drv")

grep 'I am alive!' "$res"/hello
