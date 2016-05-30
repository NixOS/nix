source common.sh

user=$(whoami)
rm -rf $TEST_HOME
mkdir -p $TEST_HOME
USER=$user $SHELL -e -c ". ../scripts/nix-profile.sh"
USER=$user $SHELL -e -c ". ../scripts/nix-profile.sh" # test idempotency

[ -L $TEST_HOME/.nix-profile ]
[ -e $TEST_HOME/.nix-channels ]
