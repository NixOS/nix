test -n "$TEST_ROOT"
if test -d "$TEST_ROOT"; then
    chmod -R u+w "$TEST_ROOT"
    rm -rf "$TEST_ROOT"
fi
mkdir "$TEST_ROOT"

mkdir "$NIX_STORE_DIR"
mkdir "$NIX_DATA_DIR"
mkdir "$NIX_LOG_DIR"
mkdir "$NIX_STATE_DIR"
mkdir "$NIX_DB_DIR"

# Initialise the database.
$TOP/src/nix-store/nix-store --init

# Did anything happen?
test -e "$NIX_DB_DIR"/validpaths
