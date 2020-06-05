source common.sh

clearStore

nix-build dependencies.nix --no-out-link
nix-build dependencies.nix --no-out-link --check

nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic --no-out-link --check 2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
[ "$status" = "104" ]

clearStore

nix-build dependencies.nix --no-out-link --repeat 3

nix-build check.nix -A nondeterministic --no-out-link --repeat 1 2> $TEST_ROOT/log || status=$?
[ "$status" = "1" ]
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

nix-build check.nix -A hashmismatch --no-out-link --hashed-mirrors '' || status=$?
[ "$status" = "102" ]

echo -n > ./dummy
nix-build check.nix -A hashmismatch --no-out-link --hashed-mirrors ''
echo 'Hello World' > ./dummy

nix-build check.nix -A hashmismatch --no-out-link --check --hashed-mirrors '' || status=$?
[ "$status" = "102" ]

# Multiple failures with --keep-going
nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic -A hashmismatch --no-out-link --check --keep-going --hashed-mirrors '' || status=$?
[ "$status" = "110" ]
