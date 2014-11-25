source common.sh

RESULT=$TEST_ROOT/result

nix-build -o $RESULT check-reqs.nix -A test1

(! nix-build -o $RESULT check-reqs.nix -A test2)
(! nix-build -o $RESULT check-reqs.nix -A test3)
(! nix-build -o $RESULT check-reqs.nix -A test4)
(! nix-build -o $RESULT check-reqs.nix -A test5)
(! nix-build -o $RESULT check-reqs.nix -A test6)

nix-build -o $RESULT check-reqs.nix -A test7
