# Regression for https://github.com/NixOS/nix/issues/5998 and https://github.com/NixOS/nix/issues/5980

source common.sh

export NIX_PATH=non-existent=/non-existent/but-unused-anyways:by-absolute-path=$PWD:by-relative-path=.

nix-instantiate --eval -E '<by-absolute-path/simple.nix>' --restrict-eval
nix-instantiate --eval -E '<by-relative-path/simple.nix>' --restrict-eval

# Should ideally also test this, but there’s no pure way to do it, so just trust me that it works
# nix-instantiate --eval -E '<nixpkgs>' -I nixpkgs=channel:nixos-unstable --restrict-eval

[[ $(nix-instantiate --find-file by-absolute-path/simple.nix) = $PWD/simple.nix ]]
[[ $(nix-instantiate --find-file by-relative-path/simple.nix) = $PWD/simple.nix ]]

tmpfile=$(mktemp)
echo "nix-path = " > "$tmpfile"

# This should still work because the NIX_PATH environment variable will take
# precedence over the empty nix path set by the configuration file
NIX_USER_CONF_FILES="$tmpfile" nix-instantiate --eval -E '<by-relative-path/simple.nix>' --restrict-eval

unset NIX_PATH

mkdir -p $TEST_ROOT/{from-nix-path-file,from-NIX_PATH,from-nix-path,from-extra-nix-path,from-I}
touch $TEST_ROOT/{from-nix-path-file,from-NIX_PATH,from-nix-path,from-extra-nix-path,from-I}/bar.nix

echo "nix-path = foo=$TEST_ROOT/from-nix-path-file" >> $NIX_CONF_DIR/nix.conf

# Use nix.conf in absence of NIX_PATH
[[ $(nix-instantiate --find-file foo/bar.nix) = $TEST_ROOT/from-nix-path-file/bar.nix ]]

# Setting nix-path on the command line with an unset NIX_PATH obeys the command-line flag
[[ $(nix-instantiate --option nix-path by-relative-path=. --find-file by-relative-path/simple.nix) = "$PWD/simple.nix" ]]
# Setting nix-path on the command line with an empty NIX_PATH also obeys the command-line flag
[[ $(NIX_PATH= nix-instantiate --option nix-path by-relative-path=. --find-file by-relative-path/simple.nix) = "$PWD/simple.nix" ]]

# NIX_PATH overrides nix.conf
[[ $(NIX_PATH=foo=$TEST_ROOT/from-NIX_PATH nix-instantiate --find-file foo/bar.nix) = $TEST_ROOT/from-NIX_PATH/bar.nix ]]
# if NIX_PATH does not have the desired entry, it fails
(! NIX_PATH=foo=$TEST_ROOT nix-instantiate --find-file foo/bar.nix)

# -I extends nix.conf
[[ $(nix-instantiate -I foo=$TEST_ROOT/from-I --find-file foo/bar.nix) = $TEST_ROOT/from-I/bar.nix ]]
# if -I does not have the desired entry, the value from nix.conf is used
[[ $(nix-instantiate -I foo=$TEST_ROOT --find-file foo/bar.nix) = $TEST_ROOT/from-nix-path-file/bar.nix ]]

# -I extends NIX_PATH
[[ $(NIX_PATH=foo=$TEST_ROOT/from-NIX_PATH nix-instantiate -I foo=$TEST_ROOT/from-I --find-file foo/bar.nix) = $TEST_ROOT/from-I/bar.nix ]]
# if -I does not have the desired entry, the value from NIX_PATH is used
[[ $(NIX_PATH=$TEST_ROOT nix-instantiate -I foo=$TEST_ROOT/from-I --find-file foo/bar.nix) = $TEST_ROOT/from-NIX_PATH/bar.nix ]]

# --extra-nix-path extends NIX_PATH
[[ $(NIX_PATH=foo=$TEST_ROOT/from-NIX_PATH nix-instantiate --extra-nix-path foo=$TEST_ROOT/from-extra-nix-path --find-file foo/bar.nix) = $TEST_ROOT/from-extra-nix-path/bar.nix ]]
# if --extra-nix-path does not have the desired entry, the value from NIX_PATH is used
[[ $(NIX_PATH=$TEST_ROOT/from-NIX_PATH nix-instantiate --extra-nix-path foo=$TEST_ROOT --find-file foo/bar.nix) = $TEST_ROOT/from-NIX_PATH/bar.nix ]]

# --nix-path overrides NIX_PATH
[[ $(NIX_PATH=foo=$TEST_ROOT/from-NIX_PATH nix-instantiate --nix-path foo=$TEST_ROOT/from-nix-path --find-file foo/bar.nix) = $TEST_ROOT/from-nix-path/bar.nix ]]
# if --nix-path does not have the desired entry, it fails
(! NIX_PATH=$TEST_ROOT/from-NIX_PATH nix-instantiate --nix-path foo=$TEST_ROOT --find-file foo/bar.nix)

# --nix-path overrides nix.conf
[[ $(nix-instantiate --nix-path foo=$TEST_ROOT/from-nix-path --find-file foo/bar.nix) = $TEST_ROOT/from-nix-path/bar.nix ]]
(! nix-instantiate --nix-path foo=$TEST_ROOT --find-file foo/bar.nix)

# --extra-nix-path extends nix.conf
[[ $(nix-instantiate --extra-nix-path foo=$TEST_ROOT/from-extra-nix-path --find-file foo/bar.nix) = $TEST_ROOT/from-extra-nix-path/bar.nix ]]
# if --extra-nix-path does not have the desired entry, it is taken from nix.conf
[[ $(nix-instantiate --extra-nix-path foo=$TEST_ROOT --find-file foo/bar.nix) = $TEST_ROOT/from-nix-path-file/bar.nix ]]

# -I extends --nix-path
[[ $(nix-instantiate --nix-path foo=$TEST_ROOT/from-nix-path -I foo=$TEST_ROOT/from-I --find-file foo/bar.nix) = $TEST_ROOT/from-I/bar.nix ]]
[[ $(nix-instantiate --nix-path foo=$TEST_ROOT/from-nix-path -I foo=$TEST_ROOT --find-file foo/bar.nix) = $TEST_ROOT/from-nix-path/bar.nix ]]
