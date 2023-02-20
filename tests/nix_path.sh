# Regression for https://github.com/NixOS/nix/issues/5998 and https://github.com/NixOS/nix/issues/5980

source common.sh

export NIX_PATH=non-existent=/non-existent/but-unused-anyways:by-absolute-path=$PWD:by-relative-path=.

nix-instantiate --eval -E '<by-absolute-path/simple.nix>' --restrict-eval
nix-instantiate --eval -E '<by-relative-path/simple.nix>' --restrict-eval

# Should ideally also test this, but there’s no pure way to do it, so just trust me that it works
# nix-instantiate --eval -E '<nixpkgs>' -I nixpkgs=channel:nixos-unstable --restrict-eval

[[ $(nix-instantiate --find-file by-absolute-path/simple.nix) = $PWD/simple.nix ]]
[[ $(nix-instantiate --find-file by-relative-path/simple.nix) = $PWD/simple.nix ]]

unset NIX_PATH

[[ $(nix-instantiate --option nix-path by-relative-path=. --find-file by-relative-path/simple.nix) = "$PWD/simple.nix" ]]
(! NIX_PATH= nix-instantiate --option nix-path by-relative-path=. --find-file by-relative-path/simple.nix)

mkdir -p $TEST_ROOT/{a,b,c,d}
touch $TEST_ROOT/{a,b,c,d}/bar.nix
touch $TEST_ROOT/c/xyzzy.nix

echo "nix-path = foo=$TEST_ROOT/a" >> $NIX_CONF_DIR/nix.conf

# Use nix.conf.
[[ $(nix-instantiate --find-file foo/bar.nix) = $TEST_ROOT/a/bar.nix ]]

# NIX_PATH overrides nix.conf.
[[ $(NIX_PATH=foo=$TEST_ROOT/b nix-instantiate --find-file foo/bar.nix) = $TEST_ROOT/b/bar.nix ]]

# -I extends NIX_PATH.
[[ $(NIX_PATH=foo=$TEST_ROOT/b nix-instantiate -I foo=$TEST_ROOT/d --find-file foo/bar.nix) = $TEST_ROOT/d/bar.nix ]]
[[ $(NIX_PATH=$TEST_ROOT nix-instantiate -I foo=$TEST_ROOT/d --find-file b/bar.nix) = $TEST_ROOT/b/bar.nix ]]

# NIX_PATH overrides --extra-nix-path.
[[ $(NIX_PATH=$TEST_ROOT nix-instantiate --extra-nix-path foo=$TEST_ROOT/c --find-file b/bar.nix) = $TEST_ROOT/b/bar.nix ]]

# --nix-path overrides nix.conf.
[[ $(nix-instantiate --nix-path foo=$TEST_ROOT/c --find-file foo/bar.nix) = $TEST_ROOT/c/bar.nix ]]

# --extra-nix-path extends nix.conf.
[[ $(nix-instantiate --extra-nix-path foo=$TEST_ROOT/c --find-file foo/bar.nix) = $TEST_ROOT/a/bar.nix ]]
[[ $(nix-instantiate --extra-nix-path foo=$TEST_ROOT/c --find-file foo/xyzzy.nix) = $TEST_ROOT/c/xyzzy.nix ]]

# -I overrides --nix-path.
[[ $(nix-instantiate --nix-path foo=$TEST_ROOT/c -I foo=$TEST_ROOT/d --find-file foo/bar.nix) = $TEST_ROOT/d/bar.nix ]]
[[ $(nix-instantiate --nix-path $TEST_ROOT -I foo=$TEST_ROOT/d --find-file c/bar.nix) = $TEST_ROOT/c/bar.nix ]]
