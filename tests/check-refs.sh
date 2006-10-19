source common.sh

set -x

RESULT=$TEST_ROOT/result

dep=$($nixbuild -o $RESULT check-refs.nix -A dep)

# test1 references dep, not itself.
test1=$($nixbuild -o $RESULT check-refs.nix -A test1)
! $nixstore -q --references $test1 | grep -q $test1
$nixstore -q --references $test1 | grep -q $dep

# test2 references src, not itself nor dep.
test2=$($nixbuild -o $RESULT check-refs.nix -A test2)
! $nixstore -q --references $test2 | grep -q $test2
! $nixstore -q --references $test2 | grep -q $dep
$nixstore -q --references $test2 | grep -q aux-ref

# test3 should fail (unallowed ref).
! $nixbuild -o $RESULT check-refs.nix -A test3

# test4 should succeed.
$nixbuild -o $RESULT check-refs.nix -A test4

# test5 should succeed.
$nixbuild -o $RESULT check-refs.nix -A test5

# test6 should fail (unallowed self-ref).
! $nixbuild -o $RESULT check-refs.nix -A test6

# test7 should succeed (allowed self-ref).
$nixbuild -o $RESULT check-refs.nix -A test7

# test8 should fail (toFile depending on derivation output).
! $nixbuild -o $RESULT check-refs.nix -A test8
