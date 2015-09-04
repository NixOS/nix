source common.sh

cp "$NIX_CONF_DIR"/nix.conf "$NIX_CONF_DIR"/nix.conf-bak
echo 'restrict-eval = true' >> "$NIX_CONF_DIR"/nix.conf

nix-instantiate ./dependencies.nix 2>&1 | grep "forbidden in restricted mode"

mv "$NIX_CONF_DIR"/nix.conf-bak "$NIX_CONF_DIR"/nix.conf
