source common.sh

# Needs the config option 'impure-env' to work
requireDaemonNewerThan "2.19.0"

enableFeatures "configurable-impure-env"
restartDaemon

varTest() {
    local var="$1"; shift
    local value="$1"; shift
    nix build --no-link -vL --argstr var "$var" --argstr value "$value" --impure "$@" --file impure-env.nix
    clearStore
}

clearStore
startDaemon

varTest env_name value --impure-env env_name=value

echo 'impure-env = set_in_config=config_value' >> "$NIX_CONF_DIR/nix.conf"
set_in_config=daemon_value restartDaemon

varTest set_in_config config_value
varTest set_in_config client_value --impure-env set_in_config=client_value

sed -i -e '/^trusted-users =/d' "$NIX_CONF_DIR/nix.conf"

env_name=daemon_value restartDaemon

varTest env_name daemon_value --impure-env env_name=client_value

killDaemon
