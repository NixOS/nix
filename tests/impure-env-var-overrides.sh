source common.sh

# Needs the config option 'impure-env-var-overrides' to work
requireDaemonNewerThan "2.18.0pre20230816"

varTest() {
    local var="$1"; shift
    local value="$1"; shift
    nix build --no-link -vL --argstr var "$var" --argstr value "$value" --impure "$@" --file impure-env-var-overrides.nix
    clearStore
}

clearStore
startDaemon

varTest env_name value --impure-env-var-overrides env_name=value

echo 'impure-env-var-overrides = set_in_config=config_value' >> "$NIX_CONF_DIR/nix.conf"
set_in_config=daemon_value restartDaemon

varTest set_in_config config_value
varTest set_in_config client_value --impure-env-var-overrides set_in_config=client_value

sed -i -e '/^trusted-users =/d' "$NIX_CONF_DIR/nix.conf"

env_name=daemon_value restartDaemon

varTest env_name daemon_value --impure-env-var-overrides env_name=client_value

killDaemon
