source common.sh

sed -e "s|@localstatedir@|$TEST_ROOT/profile-var|g" -e "s|@coreutils@|$coreutils|g" < ../scripts/nix-profile.fish.in > $TEST_ROOT/nix-profile.fish

user=$(whoami)
rm -rf $TEST_HOME $TEST_ROOT/profile-var
mkdir -p $TEST_HOME
USER=$user $SHELL -e -c ". $TEST_ROOT/nix-profile.fish; set"
USER=$user $SHELL -e -c ". $TEST_ROOT/nix-profile.fish" # test idempotency
