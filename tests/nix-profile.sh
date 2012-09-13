source common.sh

home=$TEST_ROOT/home
rm -rf $home
mkdir -p $home
HOME=$home $SHELL -e -c ". $sysconfdir/profile.d/nix.sh"
HOME=$home $SHELL -e -c ". $sysconfdir/profile.d/nix.sh" # test idempotency

[ -e $home/.nix-profile ]
[ -e $home/.nix-channels ]
