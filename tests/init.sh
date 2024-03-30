set -eu -o pipefail

source common.sh

test -n "$TEST_ROOT"
if test -d "$TEST_ROOT"; then
    chmod -R u+w "$TEST_ROOT"
    # We would delete any daemon socket, so let's stop the daemon first.
    if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]]; then
        killDaemon
    fi
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

echo 'Hello World' > ./dummy
