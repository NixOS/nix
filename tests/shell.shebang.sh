#! @ENV_PROG@ nix-shell
#! nix-shell -I nixpkgs=shell.nix --option use-substitutes false
#! nix-shell --pure -i bash -p foo bar
echo "$(foo) $(bar) $@"
