#!/usr/bin/env bash

set -eu
set -o pipefail

readonly NIX_DAEMON_DEST=/Library/LaunchDaemons/org.nixos.nix-daemon.plist
# create by default; set 0 to DIY, use a symlink, etc.
readonly NIX_VOLUME_CREATE=${NIX_VOLUME_CREATE:-1} # now default
NIX_FIRST_BUILD_UID="301"
NIX_BUILD_USER_NAME_TEMPLATE="_nixbld%d"

# caution: may update times on / if not run as normal non-root user
read_only_root() {
    # this touch command ~should~ always produce an error
    # as of this change I confirmed /usr/bin/touch emits:
    # "touch: /: Operation not permitted" Monterey
    # "touch: /: Read-only file system" Catalina+ and Big Sur
    # "touch: /: Permission denied" Mojave
    # (not matching prefix for compat w/ coreutils touch in case using
    # an explicit path causes problems; its prefix differs)
    case "$(/usr/bin/touch / 2>&1)" in
        *"Read-only file system") # Catalina, Big Sur
            return 0
            ;;
        *"Operation not permitted") # Monterey
            return 0
            ;;
        *)
            return 1
            ;;
    esac

    # Avoiding the slow semantic way to get this information (~330ms vs ~8ms)
    # unless using touch causes problems. Just in case, that approach is:
    # diskutil info -plist / | <find the Writable or WritableVolume keys>, i.e.
    # diskutil info -plist / | xmllint --xpath "name(/plist/dict/key[text()='Writable']/following-sibling::*[1])" -
}

if read_only_root && [ "$NIX_VOLUME_CREATE" = 1 ]; then
    should_create_volume() { return 0; }
else
    should_create_volume() { return 1; }
fi

# shellcheck source=./create-darwin-volume.sh
. "$EXTRACTED_NIX_PATH/create-darwin-volume.sh" "no-main"

dsclattr() {
    /usr/bin/dscl . -read "$1" \
        | /usr/bin/awk "/$2/ { print \$2 }"
}

test_nix_daemon_installed() {
  test -e "$NIX_DAEMON_DEST"
}

poly_cure_artifacts() {
    if should_create_volume; then
        task "Fixing any leftover Nix volume state"
        cat <<EOF
Before I try to install, I'll check for any existing Nix volume config
and ask for your permission to remove it (so that the installer can
start fresh). I'll also ask for permission to fix any issues I spot.
EOF
        cure_volumes
        remove_volume_artifacts
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
        nix_volume_mountd_uninstall_directions
    fi
    if test_nix_daemon_installed; then
        nix_daemon_uninstall_directions
    fi
}

poly_service_setup_note() {
    if should_create_volume; then
        echo " - create a Nix volume and a LaunchDaemon to mount it"
    fi
    echo " - create a LaunchDaemon (at $NIX_DAEMON_DEST) for nix-daemon"
    echo ""
}

poly_extra_try_me_commands() {
    :
}

poly_configure_nix_daemon_service() {
    task "Setting up the nix-daemon LaunchDaemon"
    _sudo "to set up the nix-daemon as a LaunchDaemon" \
          /bin/cp -f "/nix/var/nix/profiles/default$NIX_DAEMON_DEST" "$NIX_DAEMON_DEST"

    _sudo "to load the LaunchDaemon plist for nix-daemon" \
          launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist

    _sudo "to start the nix-daemon" \
          launchctl kickstart -k system/org.nixos.nix-daemon
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
    # This can trigger a permission prompt now:
    # "Terminal" would like to administer your computer. Administration can include modifying passwords, networking, and system settings.
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
    /usr/sbin/dseditgroup -o checkmember -m "$username" "$group" > /dev/null 2>&1
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
        header "Preparing a Nix volume"
        # intentional indent below to match task indent
        cat <<EOF
    Nix traditionally stores its data in the root directory $NIX_ROOT, but
    macOS now (starting in 10.15 Catalina) has a read-only root directory.
    To support Nix, I will create a volume and configure macOS to mount it
    at $NIX_ROOT.
EOF
        setup_darwin_volume
    fi

    if [ "$(/usr/sbin/diskutil info -plist /nix | xmllint --xpath "(/plist/dict/key[text()='GlobalPermissionsEnabled'])/following-sibling::*[1]" -)" = "<false/>" ]; then
        failure "This script needs a /nix volume with global permissions! This may require running sudo /usr/sbin/diskutil enableOwnership /nix."
    fi
}
