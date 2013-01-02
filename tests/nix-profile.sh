source common.sh

home=$TEST_ROOT/home
rm -rf $home
mkdir -p $home
HOME=$home $SHELL -e -c ". $profiledir/nix.sh"
HOME=$home $SHELL -e -c ". $profiledir/nix.sh" # test idempotency

[ -L $home/.nix-profile ]
[ -e $home/.nix-channels ]
