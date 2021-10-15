source common.sh

# 27ce722638 required some incompatible changes to the nix file, so skip this
# tests for the older versions
requireDaemonNewerThan "2.4pre20210712"

clearStore

rm -f $TEST_ROOT/result

nix-build structured-attrs.nix -A all -o $TEST_ROOT/result

[[ $(cat $TEST_ROOT/result/foo) = bar ]]
[[ $(cat $TEST_ROOT/result-dev/foo) = foo ]]

export NIX_BUILD_SHELL=$SHELL
env NIX_PATH=nixpkgs=shell.nix nix-shell structured-attrs-shell.nix \
    --run 'test -e .attrs.json; test "3" = "$(jq ".my.list|length" < $NIX_ATTRS_JSON_FILE)"'

# `nix develop` is a slightly special way of dealing with environment vars, it parses
# these from a shell-file exported from a derivation. This is to test especially `outputs`
# (which is an associative array in thsi case) being fine.
nix develop -f structured-attrs-shell.nix -c bash -c 'test -n "$out"'
