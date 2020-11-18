source common.sh

# For the post-build hook
export REMOTE_STORE_PATH=$TEST_ROOT/remote_store
export REMOTE_STORE=file://$REMOTE_STORE_PATH

cat > "$NIX_CONF_DIR"/nix.conf.extra <<EOF
trusted-users = $(whoami)
require-sigs = false
EOF

startDaemon

export NIX_REMOTE_=$NIX_REMOTE

source build.sh
source nix-copy.sh
source substitute.sh
