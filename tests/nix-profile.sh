source common.sh

home=$TEST_ROOT/home
user=$(whoami)
rm -rf $home
mkdir -p $home
HOME=$home USER=$user $SHELL -e -c ". ../scripts/nix-profile.sh"
HOME=$home USER=$user $SHELL -e -c ". ../scripts/nix-profile.sh" # test idempotency

[ -L $home/.nix-profile ]
[ -e $home/.nix-channels ]
