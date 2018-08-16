#!/bin/sh
## NOTE: If you would decide to change shebang to `/usr/bin/env sh`
## - investigate the question more to be really sure,
## as `/usr/bin/env sh` usage is less portable then `/bin/sh`,
## at least - using env is much less secure.


###############################
###  Main information
###############################
{

#
# Nix installation script
# Shell script for POSIX-comapible environments
#
###############################
#
# Upstream URL:
# https://github.com/NixOS/nix/blob/master/scripts/install-nix-from-closure.sh
#
# Currently script follows POSIX.1-2017 (POSIX is simultaneously the
# IEEE Std 1003.1™-2017 and the
# The Open Group Technical Standard Base Specifications, Issue 7)
# POSIX standard is accessible at:
# http://pubs.opengroup.org/onlinepubs/9699919799
#
# Script strives to be fully transactional, as much as shell script can be.
# That means that only after all required checks script starts to do changes.
# And if script not succeeds in some action - it catches the error and
# rolls back, if that is possible with a shell script.
#
# If you foud a way to improve the script - let us know.
#
#
# Additional notes:
# `/bin/sh -u` is not possible to do, because many Docker environments
# have unset USER variable.
#
true # so {...} body has some code, shell will not permit otherwise

}


###############################
###  Documentation
###############################
{

#
#      Installer consist of:
#      1. Setup environment
#      2. Main constants
#      3. CLI control constants
#      4. CLI output functions
#      5. Program stage functions
#      6. Main function
#      7. Invocation of the main function (aka "Start of the script")
#
#

# Special things about this script
#
# 1) Script tries to be fully POSIX compatible,
# code heavy follows that requirement.
#
# 2) Notice, Warning, Error, ErrorRevert level massages have a special
# functions. That is done to be:
#   * uniformal in output
#   * proper color highlihtgting
#   * proper message classification
#   * informative for the user
#   * convinient for use in the code
#   * code readability
#   * less function invocations
#   * to have an extendable and editable output system in a shell script
# all at the same time.
#
# Message body starts from a new line.
# And has 4 spaces from the left. Always.
#
# Code example:
###############################
#
#        notice "
#
#    Install executed for ROOT.
#
#    Doing classic Linux package manager mode.
#    In Nix this mode is called: single-user mode for root.
#
#    Nix can do multi-user mode, and manage package trees for users
#    independently.
#    "
#
###############################
#
# This is the best balance of code simplicity and code readability found so far.
#
# Output of the example (in a green color):
###############################
#
#./install: Notice:
#
#    Install executed for ROOT.
#
#    Doing classic Linux package manager mode.
#    In Nix this mode is called: single-user mode for root.
#
#    Nix can do multi-user mode, and manage package trees for users
#    independently.
#
###############################
#
true # so {...} body has some code, shell will not permit otherwise

}
set -e

dest="/nix"
self="$(dirname "$0")"
nix="@nix@"
cacert="@cacert@"


if ! [ -e "$self/.reginfo" ]; then
    echo "$0: incomplete installer (.reginfo is missing)" >&2
fi

if [ -z "$USER" ]; then
    echo "$0: \$USER is not set" >&2
    exit 1
fi

if [ -z "$HOME" ]; then
    echo "$0: \$HOME is not set" >&2
    exit 1
fi

# macOS support for 10.10 or higher
if [ "$(uname -s)" = "Darwin" ]; then
    if [ $(($(sw_vers -productVersion | cut -d '.' -f 2))) -lt 10 ]; then
        echo "$0: macOS $(sw_vers -productVersion) is not supported, upgrade to 10.10 or higher"
        exit 1
    fi
fi

# Determine if we could use the multi-user installer or not
if [ "$(uname -s)" = "Darwin" ]; then
    echo "Note: a multi-user installation is possible. See https://nixos.org/nix/manual/#sect-multi-user-installation" >&2
elif [ "$(uname -s)" = "Linux" ] && [ -e /run/systemd/system ]; then
    echo "Note: a multi-user installation is possible. See https://nixos.org/nix/manual/#sect-multi-user-installation" >&2
fi

INSTALL_MODE=no-daemon
# Trivially handle the --daemon / --no-daemon options
if [ "x${1:-}" = "x--no-daemon" ]; then
    INSTALL_MODE=no-daemon
elif [ "x${1:-}" = "x--daemon" ]; then
    INSTALL_MODE=daemon
elif [ "x${1:-}" != "x" ]; then
    (
        echo "Nix Installer [--daemon|--no-daemon]"

        echo "Choose installation method."
        echo ""
        echo " --daemon:    Installs and configures a background daemon that manages the store,"
        echo "              providing multi-user support and better isolation for local builds."
        echo "              Both for security and reproducibility, this method is recommended if"
        echo "              supported on your platform."
        echo "              See https://nixos.org/nix/manual/#sect-multi-user-installation"
        echo ""
        echo " --no-daemon: Simple, single-user installation that does not require root and is"
        echo "              trivial to uninstall."
        echo "              (default)"
        echo ""
    ) >&2
    exit
fi

if [ "$INSTALL_MODE" = "daemon" ]; then
    printf '\e[1;31mSwitching to the Daemon-based Installer\e[0m\n'
    exec "$self/install-multi-user"
    exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
    printf '\e[1;31mwarning: installing Nix as root is not supported by this script!\e[0m\n'
fi

echo "performing a single-user installation of Nix..." >&2

if ! [ -e $dest ]; then
    cmd="mkdir -m 0755 $dest && chown $USER $dest"
    echo "directory $dest does not exist; creating it by running '$cmd' using sudo" >&2
    if ! sudo sh -c "$cmd"; then
        echo "$0: please manually run '$cmd' as root to create $dest" >&2
        exit 1
    fi
fi

if ! [ -w $dest ]; then
    echo "$0: directory $dest exists, but is not writable by you. This could indicate that another user has already performed a single-user installation of Nix on this system. If you wish to enable multi-user support see http://nixos.org/nix/manual/#ssec-multi-user. If you wish to continue with a single-user install for $USER please run 'chown -R $USER $dest' as root." >&2
    exit 1
fi

mkdir -p $dest/store

printf "copying Nix to %s..." "${dest}/store" >&2

for i in $(cd "$self/store" >/dev/null && echo ./*); do
    printf "." >&2
    i_tmp="$dest/store/$i.$$"
    if [ -e "$i_tmp" ]; then
        rm -rf "$i_tmp"
    fi
    if ! [ -e "$dest/store/$i" ]; then
        cp -Rp "$self/store/$i" "$i_tmp"
        chmod -R a-w "$i_tmp"
        chmod +w "$i_tmp"
        mv "$i_tmp" "$dest/store/$i"
        chmod -w "$dest/store/$i"
    fi
done
echo "" >&2

echo "initialising Nix database..." >&2
if ! $nix/bin/nix-store --init; then
    echo "$0: failed to initialize the Nix database" >&2
    exit 1
fi

if ! "$nix/bin/nix-store" --load-db < "$self/.reginfo"; then
    echo "$0: unable to register valid paths" >&2
    exit 1
fi

. "$nix/etc/profile.d/nix.sh"

if ! "$nix/bin/nix-env" -i "$nix"; then
    echo "$0: unable to install Nix into your default profile" >&2
    exit 1
fi

# Install an SSL certificate bundle.
if [ -z "$NIX_SSL_CERT_FILE" ] || ! [ -f "$NIX_SSL_CERT_FILE" ]; then
    $nix/bin/nix-env -i "$cacert"
    export NIX_SSL_CERT_FILE="$HOME/.nix-profile/etc/ssl/certs/ca-bundle.crt"
fi

# Subscribe the user to the Nixpkgs channel and fetch it.
if ! $nix/bin/nix-channel --list | grep -q "^nixpkgs "; then
    $nix/bin/nix-channel --add https://nixos.org/channels/nixpkgs-unstable
fi
if [ -z "$_NIX_INSTALLER_TEST" ]; then
    $nix/bin/nix-channel --update nixpkgs
fi

added=
if [ -z "$NIX_INSTALLER_NO_MODIFY_PROFILE" ]; then

    # Make the shell source nix.sh during login.
    p=$HOME/.nix-profile/etc/profile.d/nix.sh

    for i in .bash_profile .bash_login .profile; do
        fn="$HOME/$i"
        if [ -w "$fn" ]; then
            if ! grep -q "$p" "$fn"; then
                echo "modifying $fn..." >&2
                echo "if [ -e $p ]; then . $p; fi # added by Nix installer" >> "$fn"
            fi
            added=1
            break
        fi
    done

fi

if [ -z "$added" ]; then
    cat >&2 <<EOF

Installation finished!  To ensure that the necessary environment
variables are set, please add the line

  . $p

to your shell profile (e.g. ~/.profile).
EOF
else
    cat >&2 <<EOF

Installation finished!  To ensure that the necessary environment
variables are set, either log in again, or type

  . $p

in your shell.
EOF
fi
