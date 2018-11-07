source common.sh

test -n "$TEST_ROOT"
if test -d "$TEST_ROOT"; then
    chmod -R u+w "$TEST_ROOT"
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
