export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

rm -rf $TEST_ROOT/case

opts="--option use-case-hack true"

# Check whether restoring and dumping a NAR that contains case
# collisions is round-tripping, even on a case-insensitive system.
nix-store $opts --restore $TEST_ROOT/case < $NIX_TEST_ROOT/case.nar
nix-store $opts --dump $TEST_ROOT/case > $TEST_ROOT/case.nar
cmp $NIX_TEST_ROOT/case.nar $TEST_ROOT/case.nar
[ "$(nix-hash $opts --type sha256 $TEST_ROOT/case)" = "$(nix-hash --flat --type sha256 $NIX_TEST_ROOT/case.nar)" ]

# Check whether we detect true collisions (e.g. those remaining after
# removal of the suffix).
touch "$TEST_ROOT/case/xt_CONNMARK.h~nix~case~hack~3"
(! nix-store $opts --dump $TEST_ROOT/case > /dev/null)
