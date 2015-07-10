source common.sh

RESULT=$TEST_ROOT/result

dep=$(nix-build -o $RESULT check-refs.nix -A dep)

# test1 references dep, not itself.
test1=$(nix-build -o $RESULT check-refs.nix -A test1)
(! nix-store -q --references $test1 | grep -q $test1)
nix-store -q --references $test1 | grep -q $dep

# test2 references src, not itself nor dep.
test2=$(nix-build -o $RESULT check-refs.nix -A test2)
(! nix-store -q --references $test2 | grep -q $test2)
(! nix-store -q --references $test2 | grep -q $dep)
nix-store -q --references $test2 | grep -q aux-ref

# test3 should fail (unallowed ref).
(! nix-build -o $RESULT check-refs.nix -A test3)

# test4 should succeed.
nix-build -o $RESULT check-refs.nix -A test4

# test5 should succeed.
nix-build -o $RESULT check-refs.nix -A test5

# test6 should fail (unallowed self-ref).
(! nix-build -o $RESULT check-refs.nix -A test6)

# test7 should succeed (allowed self-ref).
nix-build -o $RESULT check-refs.nix -A test7

# test8 should fail (toFile depending on derivation output).
(! nix-build -o $RESULT check-refs.nix -A test8)

# test9 should fail (disallowed reference).
(! nix-build -o $RESULT check-refs.nix -A test9)

# test10 should succeed (no disallowed references).
nix-build -o $RESULT check-refs.nix -A test10

# test11 should succeed (no allowed references).
nix-build -o $RESULT check-refs.nix -A test11

# test12 should fail (no allowed references).
(! nix-build -o $RESULT check-refs.nix -A test12)

# test13 should succeed (one allowed reference).
nix-build -o $RESULT check-refs.nix -A test13

# test14 should succeed (one allowed reference in out).
nix-build -o $RESULT check-refs.nix -A test14

# test15 should fail (one allowed reference in out).
(! nix-build -o $RESULT check-refs.nix -A test15)

# test16 should succeed (one allowed reference in out and two).
nix-build -o $RESULT check-refs.nix -A test16

# test17 should succeed (no disallowed references in out and two).
nix-build -o $RESULT check-refs.nix -A test17

# test18 should fail (one disallowed references in out).
(! nix-build -o $RESULT check-refs.nix -A test18)

# test19 should succeed (one disallowed references in out).
nix-build -o $RESULT check-refs.nix -A test19

# test20 should succeed (one disallowed references in out and two).
nix-build -o $RESULT check-refs.nix -A test20

# test21 should succeed (two disallows reference to one).
nix-build -o $RESULT check-refs.nix -A test20

# test22 should fail (two disallows reference to one).
(! nix-build -o $RESULT check-refs.nix -A test21)
