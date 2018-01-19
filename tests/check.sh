source common.sh

clearStore

nix-build dependencies.nix --no-out-link
nix-build dependencies.nix --no-out-link --check

nix-build check.nix -A nondeterministic --no-out-link
(! nix-build check.nix -A nondeterministic --no-out-link --check 2> $TEST_ROOT/log)
grep 'may not be deterministic' $TEST_ROOT/log

clearStore

nix-build dependencies.nix --no-out-link --repeat 3

(! nix-build check.nix -A nondeterministic --no-out-link --repeat 1 2> $TEST_ROOT/log)
grep 'differs from previous round' $TEST_ROOT/log

