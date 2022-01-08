source common.sh

# Needs the config option 'environment' to work
requireDaemonNewerThan "2.6.0pre20220109"

varTest() {
    local var="$1"; shift
    local value="$1"; shift
    nix build --no-link -vL --argstr var "$var" --argstr value "$value" --impure "$@" --file environment.nix
    clearStore
}

clearStore
startDaemon

varTest env_name value --environment env_name=value
env_name=value varTest env_name value --environment env_name

echo 'environment = env_name set_in_config=daemon_value' >> "$NIX_CONF_DIR/nix.conf"
env_name=daemon_value restartDaemon

varTest env_name daemon_value
env_name=client_value varTest env_name client_value
varTest env_name client_value --environment env_name=client_value

varTest set_in_config daemon_value
set_in_config=client_value varTest set_in_config daemon_value
varTest set_in_config client_value --environment set_in_config=client_value

sed -i -e '/^trusted-users =/d' "$NIX_CONF_DIR/nix.conf"

env_name=daemon_value restartDaemon

varTest env_name daemon_value --environment env_name=client_value
env_name=client_value varTest env_name daemon_value --environment env_name

killDaemon
