#!/bin/sh

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

# Determine if we should punt to the single-user installer or not
if [ "$(uname -s)" = "Darwin" ]; then
    INSTALL_MODE=daemon
elif [ "$(uname -s)" = "Linux" ] && [ -e /run/systemd/system ]; then
    INSTALL_MODE=daemon
else
    INSTALL_MODE=no-daemon
fi

# Trivially handle the --daemon / --no-daemon options
if [ "x${1:-}" = "x--no-daemon" ]; then
    INSTALL_MODE=no-daemon
elif [ "x${1:-}" = "x--daemon" ]; then
    INSTALL_MODE=daemon
elif [ "x${1:-}" != "x" ]; then
    (
        echo "Nix Installer [--daemon|--no-daemon]"
        echo ""
        echo " --daemon:    Force the installer to use the Daemon"
        echo "              based installer, even though it may not"
        echo "              work."
        echo ""
        echo " --no-daemon: Force a no-daemon, single-user"
        echo "              installation even when the preferred"
        echo "              method is with the daemon."
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
