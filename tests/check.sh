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

path=$(nix-build check.nix -A fetchurl --no-out-link --hashed-mirrors '')

chmod +w $path
echo foo > $path
chmod -w $path

nix-build check.nix -A fetchurl --no-out-link --check --hashed-mirrors ''

# Note: "check" doesn't repair anything, it just compares to the hash stored in the database.
[[ $(cat $path) = foo ]]

nix-build check.nix -A fetchurl --no-out-link --repair --hashed-mirrors ''

[[ $(cat $path) != foo ]]
