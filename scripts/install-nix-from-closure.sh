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


###############################
###  Setup environment
###############################
{

# Set the character collating sequence to be numeric ASCII/C standard.
readonly LC_COLLATE=C
# Set the character set to be the standard one-byte ASCII.
readonly LANG=C
# Set default umask; to be non-restrictive and friendly to others.
# umask obviously can heavy influence Nix work, and functioning of packages.
umask 022

}


###############################
###  Main constants
###############################
{

# NOTE: If you are changing the destanation from `/nix` to something other -
# please read the link:
# https://nixos.org/nixos/nix-pills/install-on-your-running-system.html#idm140737316619344
# TL;DR: Destination path change - changes the package hashes.
# Nix relies on hashes to determine packages.
# Local hashes & the central binary cache repository hashes not going to match.
# That mean that Nix going to compile packages from sources.
readonly dest='/nix'
readonly self="$(dirname "$(realpath "$0")")"
readonly nix="@nix@"
readonly cacert="@cacert@"
readonly appname="$0"

}


###############################
###  CLI control constants
###############################
{

# This chain is created to output color for normal circumstances.
# If CI desides to report to the processes that they run in a terminal,
# while at the same time CI not took effort to parse the terminal color
# commands, and also not took effort to have according to it's possibilities
# $TERM configuration record in a termcap DB - well it is really on a CI.

# If the file descriptor reports a terminal - then try to use correct DB colors
if test -t ; then
    # If terminal reports a terminfo/termcap DB of colors available - use DB.
    # Else - use literal color codes.
    if tput colors > /dev/null 2>&1 ; then
        # use tput and terminfo DB
        readonly red=$(tput setaf 1)
        readonly green=$(tput setaf 2)
        readonly yellow=$(tput setaf 3)
        readonly blue=$(tput setaf 4)
        readonly bold=$(tput smso)
        readonly reset=$(tput sgr0) # Reset to default output
    else
        # tput is not present on some systems (Alpine Linux),
        # this trick allows to store, not 'codes' - literal term command symbol.
        readonly red=$(printf '\033[1;31m')
        readonly green=$(printf '\033[1;32m')
        readonly yellow=$(printf '\033[1;33m')
        readonly blue=$(printf '\033[1;34m')
        readonly bold=$(printf '\033[1m')
        readonly reset=$(printf '\033[0;m') # Reset to default output
    fi
fi

}


###############################
###  CLI output functions
###############################
{

# NOTE: Script output corresponds to the classification of massages
# [RFC 5424](https://tools.ietf.org/html/rfc5424) - "The Syslog Protocol"
# Standard holds an industy-wide agreed criterias for messages.
# For example, systemd/journald messages fully correspond to RFC 5424.

# NOTE: Unified output function
# Every message in the script gets eventually printed by this function
print() {
    # Using `printf`, because it is more portable than `echo`.

    # Would take message from "$message", or from the first argument.
    # So using it for both flaxibly chaining for any functions and both
    # a as simple:
    # print 'Output message to user'
    # - are both possible at the same time.
    if [ -z "$message" ]; then
        message="$1"
    fi
    if [ -z "$color" ]; then
        color="$reset"
    fi
    if [ -z "$prefix" ]; then
        prefix='Info'
    fi

    # This line makes all prints in script
    # Form of message:
    # Application: Prefix: Body of message
    printf '%s%s: %s: %s%s\n' "$color" "$appname" "$prefix" "$message" "$reset"

    # At this lines, output of message is done. So unset print variables.
    unset color
    unset prefix
    unset message
}

# Since 'info' name is already taken by the well known Unix textinfo reader -
# just use 'print' for Info level messages

notice() {
    message="$1"
    color="$green"
    prefix='Notice'
    print
}

warning() {
    message="$1"
    color="$yellow"
    prefix='Warning'
    >&2 print
}

# NOTE: 'error' throws the exit signal
error() {
    message="$1"
    exitSig="$2"
    color="$red"
    prefix='Error'
    >&2 print
    if [ -z "$exitSig" ]; then
        exit 1
    fi
}

# NOTE: 'errorRevert' is a function to print messages in 'error' form
# when script catches an error and during a revert.
# It does not throw exit signal, so we can keep add to the error message
# before we decide in the end to launch the 'error' that throws exit signal.
errorRevert() {
    message="$1"
    color="$red"
    prefix='Error'
    >&2 print
}

contactUs() {
    print '

    To search/open bugreports: https://github.com/nixos/nix/issues

    To contact the team and community:
     - IRC: #nixos on irc.freenode.net
     - Twitter: @nixos_org

    Matrix community rooms: https://matrix.to/#/@nix:matrix.org
                            https://matrix.to/#/@nixos:matrix.org
    '
}

}


###############################
###  Checking requirements
###############################
{

checkingRequirements() {
# NOTE: This function - checks only, - do not make any changes to the system.
# And becouse of that and POSIx - it can be universally reused.

    checkBundle() {
        if ! [ -e "$self/.reginfo" ]; then
            error "

    Installer is incomplete ('$self/.reginfo' is missing)
    "
        fi
    }

    checkEnv() {

        if [ "$(id -u)" -eq 0 ]; then
            # TODO: At least merge single/multiuser,
            # when https://github.com/NixOS/nix/issues/1559 solved.
            # TODO: Reword after scripts integration and option switching
            # becomes clear.
            notice "

    Install executed for ROOT.

    Doing systemwide root install - this is classic Linux package manager mode.
    In Nix this mode is called: single-user mode for root.

    Nix has a multi-user mode.
    That is the main Nix mode,
    it allows users manage their own independent trees of packages.
    "
        fi

        # In case USER is not set
        # Example: running inside container
        if [ -z "$USER" ]; then
            notice "

    Environment variable USER is not set.
    "
            readonly USER="$(id -u -n)"    # id is POSIX
            print "Detected username: $USER"
        fi

        if [ -z "$HOME" ]; then
            error "

    Environment variable HOME is not set.
    "
        fi

    }

    checkHome() {

        if [ ! -e "$HOME" ]; then
            error "

    Home directory '$HOME' does not exist.
    "
        fi

    # -d also resolves symbolic soft links if they point to directory
        if [ ! -d "$HOME" ]; then
            error "

    Home directory '$HOME' is not a directory, nor a link to one.
    "
        fi

        if [ ! -w "$HOME" ]; then
            error "

    Home directory '$HOME' is not writable for user '$USER'. No deal.
    "
        fi

        # POSIX: `ls` is only able. No `test -O`, `find` can do this in POSIX
        # AWK is more portable then `cut -d'c' -fN`
        if [ "$(ls -ld "$HOME" | awk '{print $3}')" != "$USER" ]; then
            contactUs    # Let's get particular reports and solutions
            error "

    Home directory '$HOME' is not owned by user '$USER'.
    If you have legitimate case, please file a bug with description.
    We gather information on particular cases.
    "
        fi

        if [ ! -x "$HOME" ]; then
            error "

    Home directory '$HOME' is not marked as executable for user '$USER',
    how then we are going to go into it?
    "
        fi

    }

    checkDest() {

     if [ -e "$dest" ]; then
        # Destination directory exist. Nix is/was installed before, be cautious.

        # Once more -d also resolves soft links to their targets
        if [ ! -d "$dest" ]; then
            error "

    Destination '$dest' exists, but is not a directory, nor a link to one.
    "
        fi

        if [ ! -w $dest ]; then
            # Do not mindlessly help a user to chroot the directory
            # - that is a disservice.
            # Let user who does not know - at least a chanse to search and read
            # on the topic.
            # Person needs to know/find command themselves, and know about
            # consequences.
            error "

    Destination directory '$dest' exists, but is not writable for user '$USER'.

    To enable multi-user support see:
    http://nixos.org/nix/manual/#ssec-multi-user

    To nevertheless do a single-user install for '$USER':
    recursively set user '$USER' as owner for '$dest' directory.
    "
        fi

        # If checks are OK
        warning "

    Destination directory '$dest' already exists. Skipping creation of '$dest'.
    "

    fi

    if [ -e "$dest"/store ]; then

        if [ ! -d "$dest"/store ]; then
            error "

    Store directory '$dest/store' exists and it's not a directory nor a link
    to one.
    "
        fi

        if [ ! -w "$dest"/store ]; then
            error "

    Store directory '$dest/store' exists, but is not writable for user '$USER'.

    This could indicate that another user has already performed
    a single-user installation of Nix on this system.

    If you wish to enable multi-user support see:
    https://nixos.org/nix/manual/#ssec-multi-user

    To nevertheless do a single-user install for '$USER':
    recursively set user '$USER' as owner for '$dest/store' directory.
    "
        fi

    fi

    }

    # Invocation of functions
    mainCheckingRequirements() {

        checkBundle

        checkEnv

        checkHome

        checkDest

    }
}

}
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


echo "performing a single-user installation of Nix..." >&2



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
