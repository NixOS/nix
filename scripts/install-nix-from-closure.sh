#!/bin/sh

set -e

umask 0022

dest="/nix"
self="$(dirname "$0")"
nix="@nix@"
cacert="@cacert@"


if ! [ -e "$self/.reginfo" ]; then
    echo "$0: incomplete installer (.reginfo is missing)" >&2
fi

if [ -z "$USER" ] && ! USER=$(id -u -n); then
    echo "$0: \$USER is not set" >&2
    exit 1
fi

if [ -z "$HOME" ]; then
    echo "$0: \$HOME is not set" >&2
    exit 1
fi

# macOS support for 10.12.6 or higher
if [ "$(uname -s)" = "Darwin" ]; then
    macos_major=$(sw_vers -productVersion | cut -d '.' -f 2)
    macos_minor=$(sw_vers -productVersion | cut -d '.' -f 3)
    if [ "$macos_major" -lt 12 ] || { [ "$macos_major" -eq 12 ] && [ "$macos_minor" -lt 6 ]; }; then
        echo "$0: macOS $(sw_vers -productVersion) is not supported, upgrade to 10.12.6 or higher"
        exit 1
    fi
fi

# Determine if we could use the multi-user installer or not
if [ "$(uname -s)" = "Darwin" ]; then
    echo "Note: a multi-user installation is possible. See https://nixos.org/nix/manual/#sect-multi-user-installation" >&2
elif [ "$(uname -s)" = "Linux" ]; then
    echo "Note: a multi-user installation is possible. See https://nixos.org/nix/manual/#sect-multi-user-installation" >&2
fi

INSTALL_MODE=no-daemon
CREATE_DARWIN_VOLUME=0
# handle the command line flags
while [ $# -gt 0 ]; do
    case $1 in
        --daemon)
            INSTALL_MODE=daemon;;
        --no-daemon)
            INSTALL_MODE=no-daemon;;
        --no-channel-add)
            export NIX_INSTALLER_NO_CHANNEL_ADD=1;;
        --daemon-user-count)
            export NIX_USER_COUNT=$2
            shift;;
        --no-modify-profile)
            NIX_INSTALLER_NO_MODIFY_PROFILE=1;;
        --darwin-use-unencrypted-nix-store-volume)
            CREATE_DARWIN_VOLUME=1;;
        --nix-extra-conf-file)
            export NIX_EXTRA_CONF="$(cat $2)"
            shift;;
        *)
            (
                echo "Nix Installer [--daemon|--no-daemon] [--daemon-user-count INT] [--no-channel-add] [--no-modify-profile] [--darwin-use-unencrypted-nix-store-volume] [--nix-extra-conf-file FILE]"

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
                echo " --no-channel-add:    Don't add any channels. nixpkgs-unstable is installed by default."
                echo ""
                echo " --no-modify-profile: Skip channel installation. When not provided nixpkgs-unstable"
                echo "                      is installed by default."
                echo ""
                echo " --daemon-user-count: Number of build users to create. Defaults to 32."
                echo ""
                echo " --nix-extra-conf-file: Path to nix.conf to prepend when installing /etc/nix.conf"
                echo ""
            ) >&2

            # darwin and Catalina+
            if [ "$(uname -s)" = "Darwin" ] && [ "$macos_major" -gt 14 ]; then
                (
                    echo " --darwin-use-unencrypted-nix-store-volume: Create an APFS volume for the Nix"
                    echo "              store and mount it at /nix. This is the recommended way to create"
                    echo "              /nix with a read-only / on macOS >=10.15."
                    echo "              See: https://nixos.org/nix/manual/#sect-macos-installation"
                    echo ""
                ) >&2
            fi
            exit;;
    esac
    shift
done

if [ "$(uname -s)" = "Darwin" ]; then
    if [ "$CREATE_DARWIN_VOLUME" = 1 ]; then
        printf '\e[1;31mCreating volume and mountpoint /nix.\e[0m\n'
        "$self/create-darwin-volume.sh"
    fi

    info=$(diskutil info -plist / | xpath "/plist/dict/key[text()='Writable']/following-sibling::true[1]" 2> /dev/null)
    if ! [ -e $dest ] && [ -n "$info" ] && [ "$macos_major" -gt 14 ]; then
        (
            echo ""
            echo "Installing on macOS >=10.15 requires relocating the store to an apfs volume."
            echo "Use sh <(curl -L https://nixos.org/nix/install) --darwin-use-unencrypted-nix-store-volume or run the preparation steps manually."
            echo "See https://nixos.org/nix/manual/#sect-macos-installation"
            echo ""
        ) >&2
        exit 1
    fi
fi

if [ "$INSTALL_MODE" = "daemon" ]; then
    printf '\e[1;31mSwitching to the Multi-user Installer\e[0m\n'
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
    echo "$0: directory $dest exists, but is not writable by you. This could indicate that another user has already performed a single-user installation of Nix on this system. If you wish to enable multi-user support see https://nixos.org/nix/manual/#ssec-multi-user. If you wish to continue with a single-user install for $USER please run 'chown -R $USER $dest' as root." >&2
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
        cp -RPp "$self/store/$i" "$i_tmp"
        chmod -R a-w "$i_tmp"
        chmod +w "$i_tmp"
        mv "$i_tmp" "$dest/store/$i"
        chmod -w "$dest/store/$i"
    fi
done
echo "" >&2

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
if [ -z "$NIX_INSTALLER_NO_CHANNEL_ADD" ]; then
    if ! $nix/bin/nix-channel --list | grep -q "^nixpkgs "; then
        $nix/bin/nix-channel --add https://nixos.org/channels/nixpkgs-unstable
    fi
    if [ -z "$_NIX_INSTALLER_TEST" ]; then
        if ! $nix/bin/nix-channel --update nixpkgs; then
            echo "Fetching the nixpkgs channel failed. (Are you offline?)"
            echo "To try again later, run \"nix-channel --update nixpkgs\"."
        fi
    fi
fi

added=
p=$HOME/.nix-profile/etc/profile.d/nix.sh
if [ -z "$NIX_INSTALLER_NO_MODIFY_PROFILE" ]; then
    # Make the shell source nix.sh during login.
    for i in .bash_profile .bash_login .profile; do
        fn="$HOME/$i"
        if [ -w "$fn" ]; then
            if ! grep -q "$p" "$fn"; then
                echo "modifying $fn..." >&2
                echo -e "\nif [ -e $p ]; then . $p; fi # added by Nix installer" >> "$fn"
            fi
            added=1
            break
        fi
    done
    for i in .zshenv .zshrc; do
        fn="$HOME/$i"
        if [ -w "$fn" ]; then
            if ! grep -q "$p" "$fn"; then
                echo "modifying $fn..." >&2
                echo -e "\nif [ -e $p ]; then . $p; fi # added by Nix installer" >> "$fn"
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
