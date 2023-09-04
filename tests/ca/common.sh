source ../common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references/' "$NIX_CONF_DIR"/nix.conf

restartDaemon
