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

# installer allows overriding build user count to speed up installation
# as creating each user takes non-trivial amount of time on macos
readonly NIX_USER_COUNT=${NIX_USER_COUNT:-32}
readonly NIX_BUILD_GROUP_ID="30000"
readonly NIX_BUILD_GROUP_NAME="nixbld"
# darwin installer needs to override these
NIX_FIRST_BUILD_UID="30001"
NIX_BUILD_USER_NAME_TEMPLATE="nixbld%d"
# Please don't change this. We don't support it, because the
# default shell profile that comes with Nix doesn't support it.
readonly NIX_ROOT="/nix"
readonly NIX_EXTRA_CONF=${NIX_EXTRA_CONF:-}

readonly PROFILE_TARGETS=("/etc/bashrc" "/etc/profile.d/nix.sh" "/etc/zshrc" "/etc/bash.bashrc" "/etc/zsh/zshrc")
readonly PROFILE_BACKUP_SUFFIX=".backup-before-nix"
readonly PROFILE_NIX_FILE="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.sh"

readonly NIX_INSTALLED_NIX="@nix@"
readonly NIX_INSTALLED_CACERT="@cacert@"
#readonly NIX_INSTALLED_NIX="/nix/store/j8dbv5w6jl34caywh2ygdy88knx1mdf7-nix-2.3.6"
#readonly NIX_INSTALLED_CACERT="/nix/store/7dxhzymvy330i28ii676fl1pqwcahv2f-nss-cacert-3.49.2"
readonly EXTRACTED_NIX_PATH="$(dirname "$0")"

readonly ROOT_HOME=~root

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

contact_us() {
    echo "You can open an issue at https://github.com/nixos/nix/issues"
    echo ""
    echo "Or feel free to contact the team:"
    echo " - Matrix: #nix:nixos.org"
    echo " - IRC: in #nixos on irc.libera.chat"
    echo " - twitter: @nixos_org"
    echo " - forum: https://discourse.nixos.org"
}
get_help() {
    echo "We'd love to help if you need it."
    echo ""
    contact_us
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
    printf "$NIX_BUILD_USER_NAME_TEMPLATE" "$1"
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
    _textout "$RED" "$(get_help)"
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

printf -v _UNCHANGED_GRP_FMT "%b" $'\033[2m%='"$ESC" # "dim"
# bold+invert+red and bold+invert+green just for the +/- below
# red/green foreground for rest of the line
printf -v _OLD_LINE_FMT "%b" $'\033[1;7;31m-'"$ESC ${RED}%L${ESC}"
printf -v _NEW_LINE_FMT "%b" $'\033[1;7;32m+'"$ESC ${GREEN}%L${ESC}"

_diff() {
    # simple colorized diff comatible w/ pre `--color` versions
    diff --unchanged-group-format="$_UNCHANGED_GRP_FMT" --old-line-format="$_OLD_LINE_FMT" --new-line-format="$_NEW_LINE_FMT" --unchanged-line-format="  %L" "$@"
}

confirm_rm() {
    local path="$1"
    if ui_confirm "Can I remove $path?"; then
        _sudo "to remove $path" rm "$path"
    fi
}

confirm_edit() {
    local path="$1"
    local edit_path="$2"
    cat <<EOF

Nix isn't the only thing in $path,
but I think I know how to edit it out.
Here's the diff:
EOF

    # could technically test the diff, but caller should do it
    _diff "$path" "$edit_path"
    if ui_confirm "Does the change above look right?"; then
        _sudo "remove nix from $path" cp "$edit_path" "$path"
    fi
}

_SERIOUS_BUSINESS="${RED}%s:${ESC} "
password_confirm() {
    local do_something_consequential="$1"
    if ui_confirm "Can I $do_something_consequential?"; then
        # shellcheck disable=SC2059
        sudo -kv --prompt="$(printf "${_SERIOUS_BUSINESS}" "Enter your password to $do_something_consequential")"
    else
        return 1
    fi
}

# Support accumulating reminders over the course of a run and showing
# them at the end. An example where this helps: the installer changes
# something, but it won't work without a reboot. If you tell the user
# when you do it, they may miss it in the stream. The value of the
# setting isn't enough to decide whether to message because you only
# need to message if you *changed* it.

# reminders stored in array delimited by empty entry; if ! headless,
# user is asked to confirm after each delimiter.
_reminders=()
((_remind_num=1))

remind() {
    # (( arithmetic expression ))
    if (( _remind_num > 1 )); then
        header "Reminders"
        for line in "${_reminders[@]}"; do
            echo "$line"
            if ! headless && [ "${#line}" = 0 ]; then
                if read -r -p "Press enter/return to acknowledge."; then
                    printf $'\033[A\33[2K\r'
                fi
            fi
        done
    fi
}

reminder() {
    printf -v label "${BLUE}[ %d ]${ESC}" "$_remind_num"
    _reminders+=("$label")
    if [[ "$*" = "" ]]; then
        while read -r line; do
            _reminders+=("$line")
        done
    else
        # this expands each arg to an array entry (and each entry will
        # ultimately be a separate line in the output)
        _reminders+=("$@")
    fi
    _reminders+=("")
    ((_remind_num++))
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
        __sudo "$expl" "$*" >&2
    fi
    sudo "$@"
}


readonly SCRATCH=$(mktemp -d "${TMPDIR:-/tmp/}tmp.XXXXXXXXXX")
finish_cleanup() {
    rm -rf "$SCRATCH"
}

finish_fail() {
    finish_cleanup

    failure <<EOF
Jeeze, something went wrong. If you can take all the output and open
an issue, we'd love to fix the problem so nobody else has this issue.

:(
EOF
}
trap finish_fail EXIT

finish_success() {
    ok "Alright! We're done!"

    cat <<EOF
Try it! Open a new terminal, and type:
$(poly_extra_try_me_commands)
  $ nix-shell -p nix-info --run "nix-info -m"

Thank you for using this installer. If you have any feedback or need
help, don't hesitate:

$(contact_us)
EOF
    remind
    finish_cleanup
}

finish_uninstall_success() {
    ok "Alright! Nix should be removed!"

    cat <<EOF
If you spot anything this uninstaller missed or have feedback,
don't hesitate:

$(contact_us)
EOF
    remind
    finish_cleanup
}

remove_nix_artifacts() {
    failure "Not implemented yet"
}

cure_artifacts() {
    poly_cure_artifacts
    # remove_nix_artifacts (LATER)
}

validate_starting_assumptions() {
    if type nix-env 2> /dev/null >&2; then
        warning <<EOF
Nix already appears to be installed. This installer may run into issues.
If an error occurs, try manually uninstalling, then rerunning this script.

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
    task "Setting up the basic directory structure"
    if [ -d "$NIX_ROOT" ]; then
        # if /nix already exists, take ownership
        #
        # Caution: notes below are macOS-y
        # This is a bit of a goldilocks zone for taking ownership
        # if there are already files on the volume; the volume is
        # now mounted, but we haven't added a bunch of new files

        # this is probably a bit slow; I've been seeing 3.3-4s even
        # when promptly installed over a fresh single-user install.
        # In case anyone's aware of a shortcut.
        # `|| true`: .Trashes errors w/o full disk perm

        # rumor per #4488 that macOS 11.2 may not have
        # sbin on path, and that's where chown is, but
        # since this bit is cross-platform:
        # - first try with `command -vp` to try and find
        #   chown in the usual places
        # - fall back on `command -v` which would find
        #   any chown on path
        # if we don't find one, the command is already
        # hiding behind || true, and the general state
        # should be one the user can repair once they
        # figure out where chown is...
        local get_chr_own="$(command -vp chown)"
        if [[ -z "$get_chr_own" ]]; then
            get_chr_own="$(command -v chown)"
        fi
        _sudo "to take root ownership of existing Nix store files" \
              "$get_chr_own" -R "root:$NIX_BUILD_GROUP_NAME" "$NIX_ROOT" || true
    fi
    _sudo "to make the basic directory structure of Nix (part 1)" \
          install -dv -m 0755 /nix /nix/var /nix/var/log /nix/var/log/nix /nix/var/log/nix/drvs /nix/var/nix{,/db,/gcroots,/profiles,/temproots,/userpool} /nix/var/nix/{gcroots,profiles}/per-user

    _sudo "to make the basic directory structure of Nix (part 2)" \
          install -dv -g "$NIX_BUILD_GROUP_NAME" -m 1775 /nix/store

    _sudo "to place the default nix daemon configuration (part 1)" \
          install -dv -m 0555 /etc/nix
}

place_channel_configuration() {
    if [ -z "${NIX_INSTALLER_NO_CHANNEL_ADD:-}" ]; then
        echo "https://nixos.org/channels/nixpkgs-unstable nixpkgs" > "$SCRATCH/.nix-channels"
        _sudo "to set up the default system channel (part 1)" \
            install -m 0664 "$SCRATCH/.nix-channels" "$ROOT_HOME/.nix-channels"
    fi
}

welcome_to_nix() {
    ok "Welcome to the Multi-User Nix Installation"

    cat <<EOF

This installation tool will set up your computer with the Nix package
manager. This will happen in a few stages:

1. Make sure your computer doesn't already have Nix. If it does, I
   will show you instructions on how to clean up your old install.

2. Show you what I am going to install and where. Then I will ask
   if you are ready to continue.

3. Create the system users and groups that the Nix daemon uses to run
   builds.

4. Perform the basic installation of the Nix files daemon.

5. Configure your shell to import special Nix Profile files, so you
   can use Nix.

6. Start the Nix daemon.

EOF

    if ui_confirm "Would you like to see a more detailed list of what I will do?"; then
        cat <<EOF

I will:

 - make sure your computer doesn't already have Nix files
   (if it does, I will tell you how to clean them up.)
 - create local users (see the list above for the users I'll make)
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

  $ curl -L https://nixos.org/nix/install | sh

or maybe in a CI pipeline. Because of that, I'm going to skip the
verbose output in the interest of brevity.

If you would like to
see the output, try like this:

  $ curl -L -o install-nix https://nixos.org/nix/install
  $ sh ./install-nix

EOF
        return 0
    fi

    cat <<EOF
This script is going to call sudo a lot. Every time I do, it'll
output exactly what it'll do, and why.

Just like this:
EOF

    __sudo "to demonstrate how our sudo prompts look" \
           echo "this is a sudo prompt"

    cat <<EOF

This might look scary, but everything can be undone by running just a
few commands. I used to ask you to confirm each time sudo ran, but it
was too many times. Instead, I'll just ask you this one time:

EOF
    if ui_confirm "Can I use sudo?"; then
        ok "Yay! Thanks! Let's get going!"
    else
        failure <<EOF
That is okay, but I can't install.
EOF
    fi
}

install_from_extracted_nix() {
    task "Installing Nix"
    (
        cd "$EXTRACTED_NIX_PATH"

        _sudo "to copy the basic Nix files to the new store at $NIX_ROOT/store" \
              cp -RLp ./store/* "$NIX_ROOT/store/"

        _sudo "to make the new store non-writable at $NIX_ROOT/store" \
              chmod -R ugo-w "$NIX_ROOT/store/"

        if [ -d "$NIX_INSTALLED_NIX" ]; then
            echo "      Alright! We have our first nix at $NIX_INSTALLED_NIX"
        else
            failure <<EOF
Something went wrong, and I didn't find Nix installed at
$NIX_INSTALLED_NIX.
EOF
        fi

        _sudo "to load data for the first time in to the Nix Database" \
              "$NIX_INSTALLED_NIX/bin/nix-store" --load-db < ./.reginfo

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
    task "Setting up shell profiles: ${PROFILE_TARGETS[*]}"
    for profile_target in "${PROFILE_TARGETS[@]}"; do
        if [ -e "$profile_target" ]; then
            _sudo "to back up your current $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX" \
                  cp "$profile_target" "$profile_target$PROFILE_BACKUP_SUFFIX"
        else
            # try to create the file if its directory exists
            target_dir="$(dirname "$profile_target")"
            if [ -d "$target_dir" ]; then
                _sudo "to create a stub $profile_target which will be updated" \
                    touch "$profile_target"
            fi
        fi

        if [ -e "$profile_target" ]; then
            shell_source_lines \
                | _sudo "extend your $profile_target with nix-daemon settings" \
                        tee -a "$profile_target"
        fi
    done
    # TODO: should we suggest '. $PROFILE_NIX_FILE'? It would get them on
    # their way less disruptively, but a counter-argument is that they won't
    # immediately notice if something didn't get set up right?
    reminder "Nix won't work in active shell sessions until you restart them."
}

cert_in_store() {
    # in a subshell
    # - change into the cert-file dir
    # - get the phyiscal pwd
    # and test if this path is in the Nix store
    [[ "$(cd -- "$(dirname "$NIX_SSL_CERT_FILE")" && exec pwd -P)" == "$NIX_ROOT/store/"* ]]
}

setup_default_profile() {
    task "Setting up the default profile"
    _sudo "to install a bootstrapping Nix in to the default profile" \
          HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-env" -i "$NIX_INSTALLED_NIX"

    if [ -z "${NIX_SSL_CERT_FILE:-}" ] || ! [ -f "${NIX_SSL_CERT_FILE:-}" ] || cert_in_store; then
        _sudo "to install a bootstrapping SSL certificate just for Nix in to the default profile" \
              HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-env" -i "$NIX_INSTALLED_CACERT"
        export NIX_SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt
    fi

    if [ -z "${NIX_INSTALLER_NO_CHANNEL_ADD:-}" ]; then
        # Have to explicitly pass NIX_SSL_CERT_FILE as part of the sudo call,
        # otherwise it will be lost in environments where sudo doesn't pass
        # all the environment variables by default.
        if ! _sudo "to update the default channel in the default profile" \
            HOME="$ROOT_HOME" NIX_SSL_CERT_FILE="$NIX_SSL_CERT_FILE" "$NIX_INSTALLED_NIX/bin/nix-channel" --update nixpkgs; then
            reminder <<EOF
I had trouble fetching the nixpkgs channel (are you offline?)
To try again later, run: sudo -i nix-channel --update nixpkgs
EOF
        fi
    fi
}


place_nix_configuration() {
    cat <<EOF > "$SCRATCH/nix.conf"
$NIX_EXTRA_CONF
build-users-group = $NIX_BUILD_GROUP_NAME
EOF
    _sudo "to place the default nix daemon configuration (part 2)" \
          install -m 0664 "$SCRATCH/nix.conf" /etc/nix/nix.conf
}

main() {
    # TODO: I've moved this out of validate_starting_assumptions so we
    # can fail faster in this case. Sourcing install-darwin... now runs
    # `touch /` to detect Read-only root, but it could update times on
    # pre-Catalina macOS if run as root user.
    if [ $EUID -eq 0 ]; then
        failure <<EOF
Please do not run this script with root privileges. I will call sudo
when I need to.
EOF
    fi

    if [ "$(uname -s)" = "Darwin" ]; then
        # shellcheck source=./install-darwin-multi-user.sh
        . "$EXTRACTED_NIX_PATH/install-darwin-multi-user.sh"
    elif [ "$(uname -s)" = "Linux" ]; then
        # shellcheck source=./install-systemd-multi-user.sh
        . "$EXTRACTED_NIX_PATH/install-systemd-multi-user.sh" # most of this works on non-systemd distros also
    else
        failure "Sorry, I don't know what to do on $(uname)"
    fi

    welcome_to_nix
    chat_about_sudo

    cure_artifacts
    # TODO: there's a tension between cure and validate. I moved the
    # the sudo/root check out of validate to the head of this func.
    # Cure is *intended* to subsume the validate-and-abort approach,
    # so it may eventually obsolete it.
    validate_starting_assumptions

    setup_report

    if ! ui_confirm "Ready to continue?"; then
        ok "Alright, no changes have been made :)"
        get_help
        trap finish_cleanup EXIT
        exit 1
    fi

    poly_prepare_to_install

    create_build_group
    create_build_users
    create_directories
    place_channel_configuration
    install_from_extracted_nix

    configure_shell_profile

    set +eu
    # shellcheck disable=SC1091
    . /etc/profile
    set -eu

    setup_default_profile
    place_nix_configuration

    poly_configure_nix_daemon_service

    trap finish_success EXIT
}

# set an empty initial arg for bare invocations in case we need to
# disambiguate someone directly invoking this later.
if [ "${#@}" = 0 ]; then
    set ""
fi

# ACTION for override
case "${1-}" in
    # uninstall)
    #     shift
    #     uninstall "$@";;
    # install == same as the no-arg condition for now (but, explicit)
    ""|install)
        main;;
    *) # holding space for future options (like uninstall + install?)
        failure "install-multi-user: invalid argument";;
esac
