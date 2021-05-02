source common.sh

clearStore

rm -f $TEST_ROOT/result

nix-build structured-attrs.nix -A all -o $TEST_ROOT/result

[[ $(cat $TEST_ROOT/result/foo) = bar ]]
[[ $(cat $TEST_ROOT/result-dev/foo) = foo ]]

export NIX_BUILD_SHELL=$SHELL
[[ ! -e '.attrs.json' ]]
env NIX_PATH=nixpkgs=shell.nix nix-shell structured-attrs-shell.nix \
    --run 'test -e .attrs.json; test "3" = "$(jq ".my.list|length" < .attrs.json)"'
[[ ! -e '.attrs.json' ]]
