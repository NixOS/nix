#!/usr/bin/env bash

set -eu
set -o pipefail

# Sourced from:
# - https://github.com/LnL7/nix-darwin/blob/8c29d0985d74b4a990238497c47a2542a5616b3c/bootstrap.sh
# - https://gist.github.com/expipiplus1/e571ce88c608a1e83547c918591b149f/ac504c6c1b96e65505fbda437a28ce563408ecb0
# - https://github.com/NixOS/nixos-org-configurations/blob/a122f418797713d519aadf02e677fce0dc1cb446/delft/scripts/nix-mac-installer.sh
# - https://github.com/matthewbauer/macNixOS/blob/f6045394f9153edea417be90c216788e754feaba/install-macNixOS.sh
# - https://gist.github.com/LnL7/9717bd6cdcb30b086fd7f2093e5f8494/86b26f852ce563e973acd30f796a9a416248c34a
#
# however tracking which bits came from which would be impossible.

readonly ESC='\033[0m'
readonly BOLD='\033[1m'
readonly BLUE='\033[34m'
readonly BLUE_UL='\033[4;34m'
readonly GREEN='\033[32m'
readonly GREEN_UL='\033[4;32m'
readonly RED='\033[31m'

readonly NIX_USER_COUNT="32"
readonly NIX_BUILD_GROUP_ID="30000"
readonly NIX_BUILD_GROUP_NAME="nixbld"
readonly NIX_FIRST_BUILD_UID="30001"
# Please don't change this. We don't support it, because the
# default shell profile that comes with Nix doesn't support it.
readonly NIX_ROOT="/nix"

readonly PROFILE_TARGETS=("/etc/bashrc" "/etc/profile.d/nix.sh" "/etc/zshrc")
readonly PROFILE_BACKUP_SUFFIX=".backup-before-nix"
readonly PROFILE_NIX_FILE="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.sh"

readonly NIX_INSTALLED_NIX="@nix@"
readonly NIX_INSTALLED_CACERT="@cacert@"
readonly EXTRACTED_NIX_PATH="$(dirname "$0")"

readonly ROOT_HOME=$(echo ~root)

if [ -t 0 ]; then
    readonly IS_HEADLESS='no'
else
    readonly IS_HEADLESS='yes'
fi

headless() {
    if [ "$IS_HEADLESS" = "yes" ]; then
        return 0
    else
        return 1
    fi
}

contactme() {
    echo "We'd love to help if you need it."
    echo ""
    echo "If you can, open an issue at https://github.com/nixos/nix/issues"
    echo ""
    echo "Or feel free to contact the team,"
    echo " - on IRC #nixos on irc.freenode.net"
    echo " - on twitter @nixos_org"
}

uninstall_directions() {
    subheader "Uninstalling nix:"
    local step=0

    if poly_service_installed_check; then
        step=$((step + 1))
        poly_service_uninstall_directions "$step"
    fi

    for profile_target in "${PROFILE_TARGETS[@]}"; do
        if [ -e "$profile_target" ] && [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
            step=$((step + 1))
            cat <<EOF
$step. Restore $profile_target$PROFILE_BACKUP_SUFFIX back to $profile_target

  sudo mv $profile_target$PROFILE_BACKUP_SUFFIX $profile_target

(after this one, you may need to re-open any terminals that were
opened while it existed.)

EOF
        fi
    done

    step=$((step + 1))
    cat <<EOF
$step. Delete the files Nix added to your system:

  sudo rm -rf /etc/nix $NIX_ROOT $ROOT_HOME/.nix-profile $ROOT_HOME/.nix-defexpr $ROOT_HOME/.nix-channels $HOME/.nix-profile $HOME/.nix-defexpr $HOME/.nix-channels

and that is it.

EOF

}

nix_user_for_core() {
    printf "nixbld%d" "$1"
}

nix_uid_for_core() {
    echo $((NIX_FIRST_BUILD_UID + $1 - 1))
}

_textout() {
    echo -en "$1"
    shift
    if [ "$*" = "" ]; then
        cat
    else
        echo "$@"
    fi
    echo -en "$ESC"
}

header() {
    follow="---------------------------------------------------------"
    header=$(echo "---- $* $follow$follow$follow" | head -c 80)
    echo ""
    _textout "$BLUE" "$header"
}

warningheader() {
    follow="---------------------------------------------------------"
    header=$(echo "---- $* $follow$follow$follow" | head -c 80)
    echo ""
    _textout "$RED" "$header"
}

subheader() {
    echo ""
    _textout "$BLUE_UL" "$*"
}

row() {
    printf "$BOLD%s$ESC:\\t%s\\n" "$1" "$2"
}

task() {
    echo ""
    ok "~~> $1"
}

bold() {
    echo "$BOLD$*$ESC"
}

ok() {
    _textout "$GREEN" "$@"
}

warning() {
    warningheader "warning!"
    cat
    echo ""
}

failure() {
    header "oh no!"
    _textout "$RED" "$@"
    echo ""
    _textout "$RED" "$(contactme)"
    trap finish_cleanup EXIT
    exit 1
}

ui_confirm() {
    _textout "$GREEN$GREEN_UL" "$1"

    if headless; then
        echo "No TTY, assuming you would say yes :)"
        return 0
    fi

    local prompt="[y/n] "
    echo -n "$prompt"
    while read -r y; do
        if [ "$y" = "y" ]; then
            echo ""
            return 0
        elif [ "$y" = "n" ]; then
            echo ""
            return 1
        else
            _textout "$RED" "Sorry, I didn't understand. I can only understand answers of y or n"
            echo -n "$prompt"
        fi
    done
    echo ""
    return 1
}

__sudo() {
    local expl="$1"
    local cmd="$2"
    shift
    header "sudo execution"

    echo "I am executing:"
    echo ""
    printf "    $ sudo %s\\n" "$cmd"
    echo ""
    echo "$expl"
    echo ""

    return 0
}

_sudo() {
    local expl="$1"
    shift
    if ! headless; then
        __sudo "$expl" "$*"
    fi
    sudo "$@"
}


readonly SCRATCH=$(mktemp -d -t tmp.XXXXXXXXXX)
function finish_cleanup {
    rm -rf "$SCRATCH"
}

function finish_fail {
    finish_cleanup

    failure <<EOF
Jeeze, something went wrong. If you can take all the output and open
an issue, we'd love to fix the problem so nobody else has this issue.

:(
EOF
}
trap finish_fail EXIT

channel_update_failed=0
function finish_success {
    finish_cleanup

    ok "Alright! We're done!"
    if [ "x$channel_update_failed" = x1 ]; then
        echo ""
        echo "But fetching the nixpkgs channel failed. (Are you offline?)"
        echo "To try again later, run \"sudo -i nix-channel --update nixpkgs\"."
    fi
    cat <<EOF

Before Nix will work in your existing shells, you'll need to close
them and open them again. Other than that, you should be ready to go.

Try it! Open a new terminal, and type:

  $ nix-shell -p nix-info --run "nix-info -m"

Thank you for using this installer. If you have any feedback, don't
hesitate:

$(contactme)
EOF
}


validate_starting_assumptions() {
    poly_validate_assumptions

    if [ $EUID -eq 0 ]; then
        failure <<EOF
Please do not run this script with root privileges. We will call sudo
when we need to.
EOF
    fi

    if type nix-env 2> /dev/null >&2; then
        failure <<EOF
Nix already appears to be installed, and this tool assumes it is
_not_ yet installed.

$(uninstall_directions)
EOF
    fi

    if [ "${NIX_REMOTE:-}" != "" ]; then
        failure <<EOF
For some reason, \$NIX_REMOTE is set. It really should not be set
before this installer runs, and it hints that Nix is currently
installed. Please delete the old Nix installation and start again.

Note: You might need to close your shell window and open a new shell
to clear the variable.
EOF
    fi

    if echo "${SSL_CERT_FILE:-}" | grep -qE "(nix/var/nix|nix-profile)"; then
        failure <<EOF
It looks like \$SSL_CERT_FILE is set to a path that used to be part of
the old Nix installation. Please unset that variable and try again:

  $ unset SSL_CERT_FILE

EOF
    fi

    for file in ~/.bash_profile ~/.bash_login ~/.profile ~/.zshenv ~/.zprofile ~/.zshrc ~/.zlogin; do
        if [ -f "$file" ]; then
            if grep -l "^[^#].*.nix-profile" "$file"; then
                failure <<EOF
I found a reference to a ".nix-profile" in $file.
This has a high chance of breaking a new nix installation. It was most
likely put there by a previous Nix installer.

Please remove this reference and try running this again. You should
also look for similar references in:

 - ~/.bash_profile
 - ~/.bash_login
 - ~/.profile

or other shell init files that you may have.

$(uninstall_directions)
EOF
            fi
        fi
    done

    if [ -d /nix/store ] || [ -d /nix/var ]; then
        failure <<EOF
There are some relics of a previous installation of Nix at /nix, and
this scripts assumes Nix is _not_ yet installed. Please delete the old
Nix installation and start again.

$(uninstall_directions)
EOF
    fi

    if [ -d /etc/nix ]; then
        failure <<EOF
There are some relics of a previous installation of Nix at /etc/nix, and
this scripts assumes Nix is _not_ yet installed. Please delete the old
Nix installation and start again.

$(uninstall_directions)
EOF
    fi

    for profile_target in "${PROFILE_TARGETS[@]}"; do
        if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
        failure <<EOF
When this script runs, it backs up the current $profile_target to
$profile_target$PROFILE_BACKUP_SUFFIX. This backup file already exists, though.

Please follow these instructions to clean up the old backup file:

1. Copy $profile_target and $profile_target$PROFILE_BACKUP_SUFFIX to another place, just
in case.

2. Take care to make sure that $profile_target$PROFILE_BACKUP_SUFFIX doesn't look like
it has anything nix-related in it. If it does, something is probably
quite wrong. Please open an issue or get in touch immediately.

3. Take care to make sure that $profile_target doesn't look like it has
anything nix-related in it. If it does, and $profile_target _did not_,
run:

  $ /usr/bin/sudo /bin/mv $profile_target$PROFILE_BACKUP_SUFFIX $profile_target

and try again.
EOF
        fi

        if [ -e "$profile_target" ] && grep -qi "nix" "$profile_target"; then
            failure <<EOF
It looks like $profile_target already has some Nix configuration in
there. There should be no reason to run this again. If you're having
trouble, please open an issue.
EOF
        fi
    done

    danger_paths=("$ROOT_HOME/.nix-defexpr" "$ROOT_HOME/.nix-channels" "$ROOT_HOME/.nix-profile")
    for danger_path in "${danger_paths[@]}"; do
        if _sudo "making sure that $danger_path doesn't exist" \
           test -e "$danger_path"; then
            failure <<EOF
I found a file at $danger_path, which is a relic of a previous
installation. You must first delete this file before continuing.

$(uninstall_directions)
EOF
        fi
    done
}

setup_report() {
    header "Nix config report"
    row "        Temp Dir" "$SCRATCH"
    row "        Nix Root" "$NIX_ROOT"
    row "     Build Users" "$NIX_USER_COUNT"
    row "  Build Group ID" "$NIX_BUILD_GROUP_ID"
    row "Build Group Name" "$NIX_BUILD_GROUP_NAME"
    if [ "${ALLOW_PREEXISTING_INSTALLATION:-}" != "" ]; then
        row "Preexisting Install" "Allowed"
    fi

    subheader "build users:"

    row "    Username" "UID"
    for i in $(seq 1 "$NIX_USER_COUNT"); do
        row "     $(nix_user_for_core "$i")" "$(nix_uid_for_core "$i")"
    done
    echo ""
}

create_build_group() {
    local primary_group_id

    task "Setting up the build group $NIX_BUILD_GROUP_NAME"
    if ! poly_group_exists "$NIX_BUILD_GROUP_NAME"; then
        poly_create_build_group
        row "            Created" "Yes"
    else
        primary_group_id=$(poly_group_id_get "$NIX_BUILD_GROUP_NAME")
        if [ "$primary_group_id" -ne "$NIX_BUILD_GROUP_ID" ]; then
            failure <<EOF
It seems the build group $NIX_BUILD_GROUP_NAME already exists, but
with the UID $primary_group_id. This script can't really handle
that right now, so I'm going to give up.

You can fix this by editing this script and changing the
NIX_BUILD_GROUP_ID variable near the top to from $NIX_BUILD_GROUP_ID
to $primary_group_id and re-run.
EOF
        else
            row "            Exists" "Yes"
        fi
    fi
}

create_build_user_for_core() {
    local coreid
    local username
    local uid

    coreid="$1"
    username=$(nix_user_for_core "$coreid")
    uid=$(nix_uid_for_core "$coreid")

    task "Setting up the build user $username"

    if ! poly_user_exists "$username"; then
        poly_create_build_user "$username" "$uid" "$coreid"
        row "           Created" "Yes"
    else
        actual_uid=$(poly_user_id_get "$username")
        if [ "$actual_uid" != "$uid" ]; then
            failure <<EOF
It seems the build user $username already exists, but with the UID
with the UID '$actual_uid'. This script can't really handle that right
now, so I'm going to give up.

If you already created the users and you know they start from
$actual_uid and go up from there, you can edit this script and change
NIX_FIRST_BUILD_UID near the top of the file to $actual_uid and try
again.
EOF
        else
            row "            Exists" "Yes"
        fi
    fi

    if [ "$(poly_user_hidden_get "$username")" = "1" ]; then
        row "            Hidden" "Yes"
    else
        poly_user_hidden_set "$username"
        row "            Hidden" "Yes"
    fi

    if [ "$(poly_user_home_get "$username")" = "/var/empty" ]; then
        row "    Home Directory" "/var/empty"
    else
        poly_user_home_set "$username" "/var/empty"
        row "    Home Directory" "/var/empty"
    fi

    # We use grep instead of an equality check because it is difficult
    # to extract _just_ the user's note, instead it is prefixed with
    # some plist junk. This was causing the user note to always be set,
    # even if there was no reason for it.
    if ! poly_user_note_get "$username" | grep -q "Nix build user $coreid"; then
        row "              Note" "Nix build user $coreid"
    else
        poly_user_note_set "$username" "Nix build user $coreid"
        row "              Note" "Nix build user $coreid"
    fi

    if [ "$(poly_user_shell_get "$username")" = "/sbin/nologin" ]; then
        row "   Logins Disabled" "Yes"
    else
        poly_user_shell_set "$username" "/sbin/nologin"
        row "   Logins Disabled" "Yes"
    fi

    if poly_user_in_group_check "$username" "$NIX_BUILD_GROUP_NAME"; then
        row "  Member of $NIX_BUILD_GROUP_NAME" "Yes"
    else
        poly_user_in_group_set "$username" "$NIX_BUILD_GROUP_NAME"
        row "  Member of $NIX_BUILD_GROUP_NAME" "Yes"
    fi

    if [ "$(poly_user_primary_group_get "$username")" = "$NIX_BUILD_GROUP_ID" ]; then
        row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
    else
        poly_user_primary_group_set "$username" "$NIX_BUILD_GROUP_ID"
        row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
    fi
}

create_build_users() {
    for i in $(seq 1 "$NIX_USER_COUNT"); do
        create_build_user_for_core "$i"
    done
}

create_directories() {
    # FIXME: remove all of this because it duplicates LocalStore::LocalStore().

    _sudo "to make the basic directory structure of Nix (part 1)" \
          mkdir -pv -m 0755 /nix /nix/var /nix/var/log /nix/var/log/nix /nix/var/log/nix/drvs /nix/var/nix{,/db,/gcroots,/profiles,/temproots,/userpool} /nix/var/nix/{gcroots,profiles}/per-user

    _sudo "to make the basic directory structure of Nix (part 2)" \
          mkdir -pv -m 1775 /nix/store

    _sudo "to make the basic directory structure of Nix (part 3)" \
          chgrp "$NIX_BUILD_GROUP_NAME" /nix/store

    _sudo "to place the default nix daemon configuration (part 1)" \
          mkdir -pv -m 0555 /etc/nix
}

place_channel_configuration() {
    echo "https://nixos.org/channels/nixpkgs-unstable nixpkgs" > "$SCRATCH/.nix-channels"
    _sudo "to set up the default system channel (part 1)" \
          install -m 0664 "$SCRATCH/.nix-channels" "$ROOT_HOME/.nix-channels"
}

welcome_to_nix() {
    ok "Welcome to the Multi-User Nix Installation"

    cat <<EOF

This installation tool will set up your computer with the Nix package
manager. This will happen in a few stages:

1. Make sure your computer doesn't already have Nix. If it does, I
   will show you instructions on how to clean up your old one.

2. Show you what we are going to install and where. Then we will ask
   if you are ready to continue.

3. Create the system users and groups that the Nix daemon uses to run
   builds.

4. Perform the basic installation of the Nix files daemon.

5. Configure your shell to import special Nix Profile files, so you
   can use Nix.

6. Start the Nix daemon.

EOF

    if ui_confirm "Would you like to see a more detailed list of what we will do?"; then
        cat <<EOF

We will:

 - make sure your computer doesn't already have Nix files
   (if it does, I will tell you how to clean them up.)
 - create local users (see the list above for the users we'll make)
 - create a local group ($NIX_BUILD_GROUP_NAME)
 - install Nix in to $NIX_ROOT
 - create a configuration file in /etc/nix
 - set up the "default profile" by creating some Nix-related files in
   $ROOT_HOME
EOF
        for profile_target in "${PROFILE_TARGETS[@]}"; do
            if [ -e "$profile_target" ]; then
                cat <<EOF
 - back up $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX
 - update $profile_target to include some Nix configuration
EOF
            fi
        done
        poly_service_setup_note
        if ! ui_confirm "Ready to continue?"; then
            failure <<EOF
Okay, maybe you would like to talk to the team.
EOF
        fi
    fi
}

chat_about_sudo() {
    header "let's talk about sudo"

    if headless; then
        cat <<EOF
This script is going to call sudo a lot. Normally, it would show you
exactly what commands it is running and why. However, the script is
run in a headless fashion, like this:

  $ curl https://nixos.org/nix/install | sh

or maybe in a CI pipeline. Because of that, we're going to skip the
verbose output in the interest of brevity.

If you would like to
see the output, try like this:

  $ curl -o install-nix https://nixos.org/nix/install
  $ sh ./install-nix

EOF
        return 0
    fi

    cat <<EOF
This script is going to call sudo a lot. Every time we do, it'll
output exactly what it'll do, and why.

Just like this:
EOF

    __sudo "to demonstrate how our sudo prompts look" \
           echo "this is a sudo prompt"

    cat <<EOF

This might look scary, but everything can be undone by running just a
few commands. We used to ask you to confirm each time sudo ran, but it
was too many times. Instead, I'll just ask you this one time:

EOF
    if ui_confirm "Can we use sudo?"; then
        ok "Yay! Thanks! Let's get going!"
    else
        failure <<EOF
That is okay, but we can't install.
EOF
    fi
}

install_from_extracted_nix() {
    (
        cd "$EXTRACTED_NIX_PATH"

        _sudo "to copy the basic Nix files to the new store at $NIX_ROOT/store" \
              rsync -rlpt ./store/* "$NIX_ROOT/store/"

        if [ -d "$NIX_INSTALLED_NIX" ]; then
            echo "      Alright! We have our first nix at $NIX_INSTALLED_NIX"
        else
            failure <<EOF
Something went wrong, and I didn't find Nix installed at
$NIX_INSTALLED_NIX.
EOF
        fi

        cat ./.reginfo \
            | _sudo "to load data for the first time in to the Nix Database" \
                   "$NIX_INSTALLED_NIX/bin/nix-store" --load-db

        echo "      Just finished getting the nix database ready."
    )
}

shell_source_lines() {
    cat <<EOF

# Nix
if [ -e '$PROFILE_NIX_FILE' ]; then
  . '$PROFILE_NIX_FILE'
fi
# End Nix

EOF
}

configure_shell_profile() {
    # If there is an /etc/profile.d directory, we want to ensure there
    # is a nix.sh within it, so we can use the following loop to add
    # the source lines to it. Note that I'm _not_ adding the source
    # lines here, because we want to be using the regular machinery.
    #
    # If we go around that machinery, it becomes more complicated and
    # adds complications to the uninstall instruction generator and
    # old instruction sniffer as well.
    if [ -d /etc/profile.d ]; then
        _sudo "create a stub /etc/profile.d/nix.sh which will be updated" \
              touch /etc/profile.d/nix.sh
    fi

    for profile_target in "${PROFILE_TARGETS[@]}"; do
        if [ -e "$profile_target" ]; then
            _sudo "to back up your current $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX" \
                  cp "$profile_target" "$profile_target$PROFILE_BACKUP_SUFFIX"

            shell_source_lines \
                | _sudo "extend your $profile_target with nix-daemon settings" \
                        tee -a "$profile_target"
        fi
    done
}

setup_default_profile() {
    _sudo "to installing a bootstrapping Nix in to the default Profile" \
          HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-env" -i "$NIX_INSTALLED_NIX"

    if [ -z "${NIX_SSL_CERT_FILE:-}" ] || ! [ -f "${NIX_SSL_CERT_FILE:-}" ]; then
        _sudo "to installing a bootstrapping SSL certificate just for Nix in to the default Profile" \
              HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-env" -i "$NIX_INSTALLED_CACERT"
        export NIX_SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt
    fi

    # Have to explicitly pass NIX_SSL_CERT_FILE as part of the sudo call,
    # otherwise it will be lost in environments where sudo doesn't pass
    # all the environment variables by default.
    _sudo "to update the default channel in the default profile" \
          HOME="$ROOT_HOME" NIX_SSL_CERT_FILE="$NIX_SSL_CERT_FILE" "$NIX_INSTALLED_NIX/bin/nix-channel" --update nixpkgs \
          || channel_update_failed=1

}


place_nix_configuration() {
    cat <<EOF > "$SCRATCH/nix.conf"
build-users-group = $NIX_BUILD_GROUP_NAME
EOF
    _sudo "to place the default nix daemon configuration (part 2)" \
          install -m 0664 "$SCRATCH/nix.conf" /etc/nix/nix.conf
}

main() {
    if [ "$(uname -s)" = "Darwin" ]; then
        # shellcheck source=./install-darwin-multi-user.sh
        . "$EXTRACTED_NIX_PATH/install-darwin-multi-user.sh"
    elif [ "$(uname -s)" = "Linux" ]; then
        if [ -e /run/systemd/system ]; then
            # shellcheck source=./install-systemd-multi-user.sh
            . "$EXTRACTED_NIX_PATH/install-systemd-multi-user.sh"
        else
            failure "Sorry, the multi-user installation requires systemd on Linux (detected using /run/systemd/system)"
        fi
    else
        failure "Sorry, I don't know what to do on $(uname)"
    fi

    welcome_to_nix
    chat_about_sudo

    if [ "${ALLOW_PREEXISTING_INSTALLATION:-}" = "" ]; then
        validate_starting_assumptions
    fi

    setup_report

    if ! ui_confirm "Ready to continue?"; then
        ok "Alright, no changes have been made :)"
        contactme
        trap finish_cleanup EXIT
        exit 1
    fi

    create_build_group
    create_build_users
    create_directories
    place_channel_configuration
    install_from_extracted_nix

    configure_shell_profile

    set +eu
    . /etc/profile
    set -eu

    setup_default_profile
    place_nix_configuration
    poly_configure_nix_daemon_service

    trap finish_success EXIT
}


main
