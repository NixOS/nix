#!/usr/bin/env bash

set -eu
set -o pipefail

readonly NIX_DAEMON_DEST=/Library/LaunchDaemons/org.nixos.nix-daemon.plist
# create by default; set 0 to DIY, use a symlink, etc.
readonly NIX_VOLUME_CREATE=${NIX_VOLUME_CREATE:-1} # now default
readonly root_writable="$(diskutil info -plist / | xmllint --xpath "name(/plist/dict/key[text()='Writable']/following-sibling::*[1])" -)"

if [ "$root_writable" = "false" ] && [ "$NIX_VOLUME_CREATE" = 1 ]; then
    should_create_volume() { return 0; }
else
    should_create_volume() { return 1; }
fi


# TODO: I'm trying to decide how well-contained the darwin-volume
# stuff should stay here. It could merge here, in theory, but I'm reluctant
# to make that jump yet because I'm not certain that single-user macOS will
# actually go away :)
. "$EXTRACTED_NIX_PATH/create-darwin-volume.sh" "no-main"

dsclattr() {
    /usr/bin/dscl . -read "$1" \
        | awk "/$2/ { print \$2 }"
}

test_nix_daemon_installed() {
  test -e "$NIX_DAEMON_DEST"
}

poly_validate_assumptions() {
    if [ "$(uname -s)" != "Darwin" ]; then
        failure "This script is for use with macOS!"
    fi

    if should_create_volume; then
        # TODO: tentatively trying out a "curing" approach
        darwin_volume_uninstall_prompts
        # darwin_volume_validate_assumptions
    fi
}

poly_service_installed_check() {
    if should_create_volume; then
        test_nix_daemon_installed || test_nix_volume_mountd_installed
    else
        test_nix_daemon_installed
    fi
}

poly_service_uninstall_directions() {
    echo "$1. Remove macOS-specific components:"
    if should_create_volume && test_nix_volume_mountd_installed; then
        darwin_volume_uninstall_directions
    fi
    if test_nix_daemon_installed; then
        nix_daemon_uninstall_directions
    fi
}

poly_service_uninstall_prompts() {
    # TODO:
    # nixbld users
    # ~/.cache/nix
    # /var/log/nix-daemon.log
    # /var/root/
    #   .{nix-channels,nix-profile,nix-defexpr}
    #   .cache/nix
    # /etc/
    #   nix/nix.conf
    #   bashrc
    #   bashrc.backup-before-nix
    #   zshenv
    #   zshenv.backup-before-nix
    if should_create_volume && test_nix_volume_mountd_installed; then
        darwin_volume_uninstall_prompts
    fi

    if test_nix_daemon_installed; then
        nix_daemon_uninstall_prompt
    fi
}

poly_service_setup_note() {
    # TODO: add create volume (but like, conditionally)
    cat <<EOF
 - load and start a LaunchDaemon (at $NIX_DAEMON_DEST) for nix-daemon

EOF
}

poly_extra_try_me_commands(){
    :
}

poly_configure_nix_daemon_service() {
    task "Setting up the nix-daemon LaunchDaemon"
    _sudo "to set up the nix-daemon as a LaunchDaemon" \
          cp -f "/nix/var/nix/profiles/default$NIX_DAEMON_DEST" "$NIX_DAEMON_DEST"

    _sudo "to load the LaunchDaemon plist for nix-daemon" \
          launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist

    _sudo "to start the nix-daemon" \
          launchctl start org.nixos.nix-daemon
}

poly_group_exists() {
    /usr/bin/dscl . -read "/Groups/$1" > /dev/null 2>&1
}

poly_group_id_get() {
    dsclattr "/Groups/$1" "PrimaryGroupID"
}

poly_create_build_group() {
    _sudo "Create the Nix build group, $NIX_BUILD_GROUP_NAME" \
          /usr/sbin/dseditgroup -o create \
          -r "Nix build group for nix-daemon" \
          -i "$NIX_BUILD_GROUP_ID" \
          "$NIX_BUILD_GROUP_NAME" >&2
}

poly_user_exists() {
    /usr/bin/dscl . -read "/Users/$1" > /dev/null 2>&1
}

poly_user_id_get() {
    dsclattr "/Users/$1" "UniqueID"
}

poly_user_hidden_get() {
    dsclattr "/Users/$1" "IsHidden"
}

poly_user_hidden_set() {
    _sudo "in order to make $1 a hidden user" \
          /usr/bin/dscl . -create "/Users/$1" "IsHidden" "1"
}

poly_user_home_get() {
    dsclattr "/Users/$1" "NFSHomeDirectory"
}

poly_user_home_set() {
    _sudo "in order to give $1 a safe home directory" \
          /usr/bin/dscl . -create "/Users/$1" "NFSHomeDirectory" "$2"
}

poly_user_note_get() {
    dsclattr "/Users/$1" "RealName"
}

poly_user_note_set() {
    _sudo "in order to give $username a useful note" \
          /usr/bin/dscl . -create "/Users/$1" "RealName" "$2"
}

poly_user_shell_get() {
    dsclattr "/Users/$1" "UserShell"
}

poly_user_shell_set() {
    _sudo "in order to give $1 a safe home directory" \
          /usr/bin/dscl . -create "/Users/$1" "UserShell" "$2"
}

poly_user_in_group_check() {
    username=$1
    group=$2
    dseditgroup -o checkmember -m "$username" "$group" > /dev/null 2>&1
}

poly_user_in_group_set() {
    username=$1
    group=$2

    _sudo "Add $username to the $group group"\
          /usr/sbin/dseditgroup -o edit -t user \
          -a "$username" "$group"
}

poly_user_primary_group_get() {
    dsclattr "/Users/$1" "PrimaryGroupID"
}

poly_user_primary_group_set() {
    _sudo "to let the nix daemon use this user for builds (this might seem redundant, but there are two concepts of group membership)" \
          /usr/bin/dscl . -create "/Users/$1" "PrimaryGroupID" "$2"
}

poly_create_build_user() {
    username=$1
    uid=$2
    builder_num=$3

    _sudo "Creating the Nix build user (#$builder_num), $username" \
          /usr/bin/dscl . create "/Users/$username" \
          UniqueID "${uid}"
}

poly_prepare_to_install() {
    if should_create_volume; then
        task "Creating a Nix volume"
        # intentional indent below to match task indent
        cat <<EOF
    Nix traditionally stores its data in the root directory $NIX_ROOT, but
    macOS now (starting in 10.15 Catalina) has a read-only root directory.
    To support Nix, I will create a volume and configure macOS to mount it
    at $NIX_ROOT.
EOF
        setup_darwin_volume
    fi
}
