source common.sh

file=build-hook-ca-floating.nix

sed -i 's/experimental-features .*/& ca-derivations/' "$NIX_CONF_DIR"/nix.conf

CONTENT_ADDRESSED=true

source build-remote.sh
