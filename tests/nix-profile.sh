export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

user=$(whoami)
export NIX_USER_PROFILE_DIR="$TEST_ROOT/profile-var/nix/profiles/per-user/$user"

rm -rf "$TEST_HOME" "$TEST_ROOT/profile-var"
mkdir -p "$TEST_HOME"

source ./nix-profile.sh
source ./nix-profile.sh # test idempotency

[ -L $TEST_HOME/.nix-profile ] 
[ -e $TEST_HOME/.nix-channels ] 
[ -e $NIX_STATE_DIR/gcroots/per-user/$user ] 
[ -e $TEST_ROOT/profile-var/nix/profiles/per-user/$user ] 