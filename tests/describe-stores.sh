source common.sh

# Query an arbitrary value in `nix describe-stores --json`'s output just to
# check that it has the right structure
[[ $(nix --experimental-features 'nix-command flakes' describe-stores --json | jq '.["SSH Store"]["compress"]["defaultValue"]') == false ]]

# Ensure that the output of `nix describe-stores` isn't empty
[[ -n $(nix --experimental-features 'nix-command flakes' describe-stores) ]]
