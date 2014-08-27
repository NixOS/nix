source common.sh

RESULT=$TEST_ROOT/result

# test1 should succeed.
nix-build -o $RESULT check-reqs.nix -A test1

# test{2,3,4,5} should fail.
(! nix-build -o $RESULT check-reqs.nix -A test2)
(! nix-build -o $RESULT check-reqs.nix -A test3)
(! nix-build -o $RESULT check-reqs.nix -A test4)
(! nix-build -o $RESULT check-reqs.nix -A test5)
