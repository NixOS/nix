# shellcheck shell=bash
source ../common.sh

# Need backend to support revamped CA
requireDaemonNewerThan "2.35.0pre20260303"

enableFeatures "ca-derivations"

signIfNeeded() {
    # Daemon signature checking
    if [[ "$NIX_REMOTE" == "daemon" ]]; then
        nix-store --generate-binary-cache-key cache.example.org "$TEST_ROOT/sk" "$TEST_ROOT/pk"
        pk=$(cat "$TEST_ROOT/pk")
        cat <<EOF >> "$test_nix_conf"
secret-key-files = $TEST_ROOT/sk
trusted-public-keys = $pk
EOF
        restartDaemon
    fi
}

TODO_NixOS

restartDaemon
