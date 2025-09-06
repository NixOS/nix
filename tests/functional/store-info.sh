#!/usr/bin/env bash

source common.sh

# Different versions of the Nix daemon normalize or don't normalize
# store URLs, plus NIX_REMOTE (per the test suite) might not be using on
# store URL in normal form, so the easiest thing to do is normalize URLs
# after the fact before comparing them for equality.
normalize_nix_store_url () {
    local url="$1"
    case "$url" in
        'auto' )
            # Need to actually ask Nix in this case
            echo "$defaultStore"
            ;;
        local | 'local://' )
            echo 'local'
            ;;
        daemon | 'unix://' )
            echo 'daemon'
            ;;
        'local://'* )
            # To not be captured by next pattern
            echo "$url"
            ;;
        'local?'* )
            echo "local://${url#local}"
            ;;
        'daemon?'* )
            echo "unix://${url#daemon}"
            ;;
        * )
            echo "$url"
            ;;
    esac
}

STORE_INFO=$(nix store info 2>&1)
LEGACY_STORE_INFO=$(nix store ping 2>&1) # alias to nix store info
STORE_INFO_JSON=$(nix store info --json)

defaultStore="$(normalize_nix_store_url "$(echo "$STORE_INFO_JSON" | jq -r ".url")")"

# Test cases for `normalize_nix_store_url` itself

# Normalize local store
[[ "$(normalize_nix_store_url "local://")" = "local" ]]
[[ "$(normalize_nix_store_url "local")" = "local" ]]
[[ "$(normalize_nix_store_url "local?foo=bar")" = "local://?foo=bar" ]]

# Normalize unix domain socket remote store
[[ "$(normalize_nix_store_url "unix://")" = "daemon" ]]
[[ "$(normalize_nix_store_url "daemon")" = "daemon" ]]
[[ "$(normalize_nix_store_url "daemon?x=y")" = "unix://?x=y" ]]

# otherwise unchanged
[[ "$(normalize_nix_store_url "https://site")" = "https://site" ]]

nixRemoteOrDefault=$(normalize_nix_store_url "${NIX_REMOTE:-"auto"}")

check_human_readable () {
    [[ "$(normalize_nix_store_url "$(echo "$1" | grep 'Store URL:' | sed 's^Store URL: ^^')")" = "${nixRemoteOrDefault}" ]]
}
check_human_readable "$STORE_INFO"
check_human_readable "$LEGACY_STORE_INFO"

if [[ -v NIX_DAEMON_PACKAGE ]] && isDaemonNewer "2.7.0pre20220126"; then
    DAEMON_VERSION=$("$NIX_DAEMON_PACKAGE"/bin/nix daemon --version | cut -d' ' -f3)
    echo "$STORE_INFO" | grep "Version: $DAEMON_VERSION"
    [[ "$(echo "$STORE_INFO_JSON" | jq -r ".version")" == "$DAEMON_VERSION" ]]
fi


expect 127 NIX_REMOTE=unix:"$PWD"/store nix store info || \
    fail "nix store info on a non-existent store should fail"

TODO_NixOS

[[ "$(normalize_nix_store_url "$(echo "$STORE_INFO_JSON" | jq -r ".url")")" == "${nixRemoteOrDefault}" ]]
