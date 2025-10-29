#! @ENV_PROG@ nix-shell
#! nix-shell -I nixpkgs=shell.nix --no-substitute
#! nix-shell --pure -i bash -p foo bar
# shellcheck shell=bash
echo "$(foo) $(bar)" "$@"
