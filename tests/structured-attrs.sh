source common.sh

clearStore

rm -f $TEST_ROOT/result

nix-build structured-attrs.nix -A all -o $TEST_ROOT/result

[[ $(cat $TEST_ROOT/result/foo) = bar ]]
[[ $(cat $TEST_ROOT/result-dev/foo) = foo ]]
