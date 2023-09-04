# Regression for https://github.com/NixOS/nix/issues/5998 and https://github.com/NixOS/nix/issues/5980

source common.sh

export NIX_PATH=non-existent=/non-existent/but-unused-anyways:by-absolute-path=$PWD:by-relative-path=.

nix-instantiate --eval -E '<by-absolute-path/simple.nix>' --restrict-eval
nix-instantiate --eval -E '<by-relative-path/simple.nix>' --restrict-eval

# Should ideally also test this, but there’s no pure way to do it, so just trust me that it works
# nix-instantiate --eval -E '<nixpkgs>' -I nixpkgs=channel:nixos-unstable --restrict-eval
