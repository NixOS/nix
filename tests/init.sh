source common.sh

if isTestOnSystemNix then
    # The test framework is written to work with a single store, of which the
    # details depend on the environment where the tests run.
    # That makes reconfiguring just this test to init a _different_ store when
    # running on NixOS harder than necessary; not currently worthwhile; tech debt.
    # So, we rely on the regular in-derivation package tests to test the ability
    # to initialize a store.
    exit 99
fi

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
