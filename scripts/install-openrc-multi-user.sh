#!/usr/bin/env bash

set -eu
set -o pipefail

# System specific settings
export NIX_FIRST_BUILD_UID="${NIX_FIRST_BUILD_UID:-30001}"
export NIX_BUILD_GROUP_ID="${NIX_BUILD_GROUP_ID:-30000}"
export NIX_BUILD_USER_NAME_TEMPLATE="nixbld%d"

readonly SERVICE_SRC=/lib/openrc/nix-daemon
readonly SERVICE_DEST=/etc/init.d/nix-daemon
# Path for the override variables file to contain the proxy settings
readonly SERVICE_OVERRIDE=/etc/conf.d/nix-daemon

create_openrc_override() {
    # clear the file if it already exists
    : > $SERVICE_OVERRIDE
    for v in $1; do
        if [ -n "${!v:-}" ]; then
            echo "$v=${!v}" >> $SERVICE_OVERRIDE
        fi
    done
}

handle_network_proxy() {
    header "Configuring proxy for the nix-daemon service"
    # Create an openrc override with proxy environment variables
    # if any proxy environment variables are not empty.
    vars="http_proxy https_proxy ftp_proxy all_proxy no_proxy HTTP_PROXY HTTPS_PROXY FTP_PROXY ALL_PROXY NO_PROXY"
    for v in $vars; do
        if [ -n "${!v:-}" ]; then
            _sudo "create openrc env overide file" \
                create_openrc_override "$vars"
            break
        fi
    done
}

poly_cure_artifacts() {
    :
}

poly_service_installed_check() {
    rc-service nix-daemon status | grep -q 'status: started'
}

poly_service_uninstall_directions() {
        cat <<EOF
$1. Remove and delete the openrc service

  sudo rc-service nix-daemon stop
  sudo rc-service del nix-daemon
  sudo rm -f $SERVICE_DEST
  sudo rm -f $SERVICE_OVERRIDE
EOF
}

poly_service_setup_note() {
    cat <<EOF
 - load and start a service (at $SERVICE_DEST
   ) for nix-daemon

EOF
}

poly_extra_try_me_commands() {
    if command -v rc-status > /dev/null; then
        :
    else
        cat <<EOF
  $ sudo nix-daemon
EOF
    fi
}

poly_configure_nix_daemon_service() {
    task "Setting up the nix-daemon openrc service"

    _sudo "to create the nix-daemon service script" \
        ln -sfn "/nix/var/nix/profiles/default$SERVICE_SRC" "$SERVICE_DEST"

    _sudo "to set permissions on the nix-daemon service" \
        chmod a+rx $SERVICE_DEST

    _sudo "to set up the nix-daemon service" \
        rc-update add nix-daemon

    handle_network_proxy

    _sudo "to start the nix-daemon.service" \
        rc-service nix-daemon start
}

poly_group_exists() {
    getent group "$1" > /dev/null 2>&1
}

poly_group_id_get() {
    getent group "$1" | cut -d: -f3
}

poly_create_build_group() {
    _sudo "Create the Nix build group, $NIX_BUILD_GROUP_NAME" \
          groupadd -g "$NIX_BUILD_GROUP_ID" --system \
          "$NIX_BUILD_GROUP_NAME" >&2
}

poly_user_exists() {
    getent passwd "$1" > /dev/null 2>&1
}

poly_user_id_get() {
    getent passwd "$1" | cut -d: -f3
}

poly_user_hidden_get() {
    echo "1"
}

poly_user_hidden_set() {
    true
}

poly_user_home_get() {
    getent passwd "$1" | cut -d: -f6
}

poly_user_home_set() {
    _sudo "in order to give $1 a safe home directory" \
          usermod --home "$2" "$1"
}

poly_user_note_get() {
    getent passwd "$1" | cut -d: -f5
}

poly_user_note_set() {
    _sudo "in order to give $1 a useful comment" \
          usermod --comment "$2" "$1"
}

poly_user_shell_get() {
    getent passwd "$1" | cut -d: -f7
}

poly_user_shell_set() {
    _sudo "in order to prevent $1 from logging in" \
          usermod --shell "$2" "$1"
}

poly_user_in_group_check() {
    groups "$1" | grep -q "$2" > /dev/null 2>&1
}

poly_user_in_group_set() {
    _sudo "Add $1 to the $2 group"\
          usermod --append --groups "$2" "$1"
}

poly_user_primary_group_get() {
    getent passwd "$1" | cut -d: -f4
}

poly_user_primary_group_set() {
    _sudo "to let the nix daemon use this user for builds (this might seem redundant, but there are two concepts of group membership)" \
          usermod --gid "$2" "$1"

}

poly_create_build_user() {
    username=$1
    uid=$2
    builder_num=$3

    _sudo "Creating the Nix build user, $username" \
          useradd \
          --home-dir /var/empty \
          --comment "Nix build user $builder_num" \
          --gid "$NIX_BUILD_GROUP_ID" \
          --groups "$NIX_BUILD_GROUP_NAME" \
          --no-user-group \
          --system \
          --shell /sbin/nologin \
          --uid "$uid" \
          --password "!" \
          "$username"
}

poly_prepare_to_install() {
    :
}

