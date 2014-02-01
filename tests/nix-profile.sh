source common.sh

home=$TEST_ROOT/home
rm -rf $home
mkdir -p $home
HOME=$home $SHELL -e -c ". ../scripts/nix-profile.sh"
HOME=$home $SHELL -e -c ". ../scripts/nix-profile.sh" # test idempotency

[ -L $home/.nix-profile ]
[ -e $home/.nix-channels ]
