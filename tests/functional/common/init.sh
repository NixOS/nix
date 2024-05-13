test -n "$TEST_ROOT"
# We would delete any daemon socket, so let's stop the daemon first.
killDaemon
# Destroy the test directory that may have persisted from previous runs
rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT"
mkdir "$TEST_HOME"

mkdir "$NIX_STORE_DIR"
mkdir "$NIX_LOCALSTATE_DIR"
mkdir -p "$NIX_LOG_DIR"/drvs
mkdir "$NIX_STATE_DIR"
mkdir "$NIX_CONF_DIR"

cat > "$NIX_CONF_DIR"/nix.conf <<EOF
build-users-group =
keep-derivations = false
sandbox = false
experimental-features = nix-command
gc-reserved-space = 0
substituters =
flake-registry = $TEST_ROOT/registry.json
show-trace = true
include nix.conf.extra
trusted-users = $(whoami)
EOF

cat > "$NIX_CONF_DIR"/nix.conf.extra <<EOF
fsync-metadata = false
extra-experimental-features = flakes
!include nix.conf.extra.not-there
EOF

# Initialise the database.
# The flag itself does nothing, but running the command touches the store
nix-store --init
# Sanity check
test -e "$NIX_STATE_DIR"/db/db.sqlite
