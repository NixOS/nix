# Don't start the daemon
source common/vars-and-functions.sh

test -n "$TEST_ROOT"
if test -d "$TEST_ROOT"; then
    chmod -R u+rw "$TEST_ROOT"
    # We would delete any daemon socket, so let's stop the daemon first.
    killDaemon
    rm -rf "$TEST_ROOT"
fi
mkdir "$TEST_ROOT"

mkdir "$NIX_STORE_DIR"
mkdir "$NIX_LOCALSTATE_DIR"
mkdir -p "$NIX_LOG_DIR"/drvs
mkdir "$NIX_STATE_DIR"
mkdir "$NIX_CONF_DIR"

cat > "$NIX_CONF_DIR"/nix.conf <<EOF
build-users-group =
keep-derivations = false
sandbox = false
experimental-features = nix-command flakes
gc-reserved-space = 0
substituters =
flake-registry = $TEST_ROOT/registry.json
show-trace = true
include nix.conf.extra
trusted-users = $(whoami)
EOF

cat > "$NIX_CONF_DIR"/nix.conf.extra <<EOF
fsync-metadata = false
!include nix.conf.extra.not-there
EOF

# Initialise the database.
nix-store --init

# Did anything happen?
test -e "$NIX_STATE_DIR"/db/db.sqlite
