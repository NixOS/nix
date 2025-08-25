#!/usr/bin/env bash

set -eu
set -o pipefail

# System specific settings
# FreeBSD typically uses UIDs from 1001+ for regular users,
# so we'll use a range that's unlikely to conflict
export NIX_FIRST_BUILD_UID="${NIX_FIRST_BUILD_UID:-30001}"
export NIX_BUILD_GROUP_ID="${NIX_BUILD_GROUP_ID:-30000}"
export NIX_BUILD_USER_NAME_TEMPLATE="nixbld%d"

# FreeBSD service paths
readonly SERVICE_SRC=/etc/rc.d/nix-daemon
readonly SERVICE_DEST=/usr/local/etc/rc.d/nix-daemon

poly_cure_artifacts() {
    :
}

poly_service_installed_check() {
    if [ -f "$SERVICE_DEST" ]; then
        return 0
    else
        return 1
    fi
}

poly_service_uninstall_directions() {
    cat <<EOF
$1. Delete the rc.d service

  sudo service nix-daemon stop
  sudo rm -f $SERVICE_DEST
  sudo sysrc -x nix_daemon_enable

EOF
}

poly_service_setup_note() {
    cat <<EOF
 - link the nix-daemon rc.d service to $SERVICE_DEST

EOF
}

poly_extra_try_me_commands() {
    cat <<EOF
  $ sudo service nix-daemon start
EOF
}

poly_configure_nix_daemon_service() {
    task "Setting up the nix-daemon rc.d service"

    # Ensure the rc.d directory exists
    _sudo "to create the rc.d directory" \
          mkdir -p /usr/local/etc/rc.d

    # Link the pre-installed rc.d script
    _sudo "to set up the nix-daemon service" \
          ln -sfn "/nix/var/nix/profiles/default$SERVICE_SRC" "$SERVICE_DEST"

    _sudo "to enable the nix-daemon service" \
          sysrc nix_daemon_enable=YES

    _sudo "to start the nix-daemon" \
          service nix-daemon start
}

poly_group_exists() {
    pw group show "$1" > /dev/null 2>&1
}

poly_group_id_get() {
    pw group show "$1" | cut -d: -f3
}

poly_create_build_group() {
    _sudo "Create the Nix build group, $NIX_BUILD_GROUP_NAME" \
          pw groupadd -n "$NIX_BUILD_GROUP_NAME" -g "$NIX_BUILD_GROUP_ID" >&2
}

poly_user_exists() {
    pw user show "$1" > /dev/null 2>&1
}

poly_user_id_get() {
    pw user show "$1" | cut -d: -f3
}

poly_user_hidden_get() {
    # FreeBSD doesn't have a concept of hidden users like macOS
    echo "0"
}

poly_user_hidden_set() {
    # No-op on FreeBSD
    true
}

poly_user_home_get() {
    pw user show "$1" | cut -d: -f9
}

poly_user_home_set() {
    _sudo "in order to give $1 a safe home directory" \
          pw usermod -n "$1" -d "$2"
}

poly_user_note_get() {
    pw user show "$1" | cut -d: -f8
}

poly_user_note_set() {
    _sudo "in order to give $1 a useful comment" \
          pw usermod -n "$1" -c "$2"
}

poly_user_shell_get() {
    pw user show "$1" | cut -d: -f10
}

poly_user_shell_set() {
    _sudo "in order to prevent $1 from logging in" \
          pw usermod -n "$1" -s "$2"
}

poly_user_in_group_check() {
    groups "$1" 2>/dev/null | grep -q "\<$2\>"
}

poly_user_in_group_set() {
    _sudo "Add $1 to the $2 group" \
          pw groupmod -n "$2" -m "$1"
}

poly_user_primary_group_get() {
    pw user show "$1" | cut -d: -f4
}

poly_user_primary_group_set() {
    _sudo "to let the nix daemon use this user for builds" \
          pw usermod -n "$1" -g "$2"
}

poly_create_build_user() {
    username=$1
    uid=$2
    builder_num=$3

    _sudo "Creating the Nix build user, $username" \
          pw useradd \
          -n "$username" \
          -u "$uid" \
          -g "$NIX_BUILD_GROUP_NAME" \
          -G "$NIX_BUILD_GROUP_NAME" \
          -d /var/empty \
          -s /sbin/nologin \
          -c "Nix build user $builder_num"
}

poly_prepare_to_install() {
    # FreeBSD-specific preparation steps
    :
}

poly_configure_default_profile_targets() {
    # FreeBSD-specific profile locations
    # FreeBSD uses /usr/local/etc for third-party shell configurations
    # Include both profile (for login shells) and bashrc (for interactive shells)
    echo "/usr/local/etc/profile /usr/local/etc/bashrc /usr/local/etc/profile.d/nix.sh /usr/local/etc/zshrc"
}
