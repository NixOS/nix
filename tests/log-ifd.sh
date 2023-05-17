#
source common.sh

# TODO: Now what
nix-build ./import-derivation.nix --option "log-import-from-derivation" true --no-out-link --log-format internal-json 2>&1 1>/dev/null | grep "^@nix" | sed 's/^@nix //g' | jq "select(.type == 112)"
