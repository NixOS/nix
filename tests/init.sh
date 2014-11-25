source common.sh

echo "NIX_STORE_DIR=$NIX_STORE_DIR NIX_DB_DIR=$NIX_DB_DIR"

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
mkdir "$NIX_DB_DIR"
mkdir "$NIX_CONF_DIR"

cat > "$NIX_CONF_DIR"/nix.conf <<EOF
build-users-group =
gc-keep-outputs = false
gc-keep-derivations = false
env-keep-derivations = false
fsync-metadata = false
EOF

# Initialise the database.
nix-store --init

# Did anything happen?
test -e "$NIX_DB_DIR"/db.sqlite

echo 'Hello World' > ./dummy
