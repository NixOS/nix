#!/usr/bin/env bash

source common.sh

# Needs the config option 'impure-env' to work
requireDaemonNewerThan "2.19.0"

TODO_NixOS

enableFeatures "configurable-impure-env"
restartDaemon

varTest() {
    local var="$1"; shift
    local value="$1"; shift
    nix build --no-link -vL --argstr var "$var" --argstr value "$value" --impure "$@" --file impure-env.nix
    clearStore
}

startDaemon

varTest env_name value --impure-env env_name=value

echo 'impure-env = set_in_config=config_value' >> "$test_nix_conf"
set_in_config=daemon_value restartDaemon

varTest set_in_config config_value
varTest set_in_config client_value --impure-env set_in_config=client_value

if [[ $with_secretspec == true ]]; then
    cat > "$TEST_ROOT/secretspec.toml" <<EOF
[project]
name = "nix-impure-env-test"
revision = "1.0"

[profiles.nix]
IMPURE_VALUE = { description = "impure environment test value", required = true }
EOF
    echo 'IMPURE_VALUE=secret_value' > "$TEST_ROOT/secrets.env"

    # A trusted client must forward both the mapping and the SecretSpec context to
    # the daemon. The daemon intentionally has no SecretSpec context at this point.
    varTest from_secret secret_value \
        --secretspec-file "$TEST_ROOT/secretspec.toml" \
        --secretspec-provider "dotenv://$TEST_ROOT/secrets.env" \
        --secretspec-profile nix \
        --secretspec-impure-env from_secret=IMPURE_VALUE

    cat >> "$test_nix_conf" <<EOF
secretspec-file = $TEST_ROOT/secretspec.toml
secretspec-provider = dotenv://$TEST_ROOT/secrets.env
secretspec-profile = nix
secretspec-impure-env = from_secret=IMPURE_VALUE
EOF
    restartDaemon

    varTest from_secret secret_value
fi

sed -i -e '/^trusted-users =/d' "$test_nix_conf"

env_name=daemon_value restartDaemon

varTest env_name daemon_value --impure-env env_name=client_value

if [[ $with_secretspec == true ]]; then
    # An untrusted client must not consume the daemon's SecretSpec mapping.
    varTest from_secret ""
fi

killDaemon
