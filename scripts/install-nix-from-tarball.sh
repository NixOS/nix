#!/bin/sh

set -e

umask 0022

dest="/nix"
self="$(dirname "$0")"
nix="@nix@"
cacert="@cacert@"

# allow to override identity change command
readonly NIX_BECOME="${NIX_BECOME:-sudo}"

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

OS="$(uname -s)"

# macOS support for 10.12.6 or higher
if [ "$OS" = "Darwin" ]; then
    IFS='.' read -r macos_major macos_minor macos_patch << EOF
$(sw_vers -productVersion)
EOF
    if [ "$macos_major" -lt 10 ] || { [ "$macos_major" -eq 10 ] && [ "$macos_minor" -lt 12 ]; } || { [ "$macos_minor" -eq 12 ] && [ "$macos_patch" -lt 6 ]; }; then
        # patch may not be present; command substitution for simplicity
        echo "$0: macOS $(sw_vers -productVersion) is not supported, upgrade to 10.12.6 or higher"
        exit 1
    fi
fi

# Determine if we could use the multi-user installer or not
if [ "$OS" = "Linux" ] || [ "$OS" = "FreeBSD" ]; then
    echo "Note: a multi-user installation is possible. See https://nix.dev/manual/nix/stable/installation/installing-binary.html#multi-user-installation" >&2
fi

case "$OS" in
    "Darwin")
        INSTALL_MODE=daemon;;
    *)
        INSTALL_MODE=no-daemon;;
esac

ACTION=

# handle the command line flags
while [ $# -gt 0 ]; do
    case $1 in
        --daemon)
            INSTALL_MODE=daemon
            ACTION=install
            ;;
        --no-daemon)
            if [ "$OS" = "Darwin" ]; then
                printf '\e[1;31mError: --no-daemon installs are no-longer supported on Darwin/macOS!\e[0m\n' >&2
                exit 1
            fi
            INSTALL_MODE=no-daemon
            ACTION=install
            ;;
        --yes)
            export NIX_INSTALLER_YES=1;;
        --no-channel-add)
            export NIX_INSTALLER_NO_CHANNEL_ADD=1;;
        --daemon-user-count)
            export NIX_USER_COUNT="$2"
            shift;;
        --no-modify-profile)
            NIX_INSTALLER_NO_MODIFY_PROFILE=1;;
        --darwin-use-unencrypted-nix-store-volume)
            {
                echo "Warning: the flag --darwin-use-unencrypted-nix-store-volume"
                echo "         is no longer needed and will be removed in the future."
                echo ""
            } >&2;;
        --nix-extra-conf-file)
            # shellcheck disable=SC2155
            export NIX_EXTRA_CONF="$(cat "$2")"
            shift;;
        *)
            {
                echo "Nix Installer [--daemon|--no-daemon] [--daemon-user-count INT] [--yes] [--no-channel-add] [--no-modify-profile] [--nix-extra-conf-file FILE]"

                echo "Choose installation method."
                echo ""
                echo " --daemon:    Installs and configures a background daemon that manages the store,"
                echo "              providing multi-user support and better isolation for local builds."
                echo "              Both for security and reproducibility, this method is recommended if"
                echo "              supported on your platform."
                echo "              See https://nix.dev/manual/nix/stable/installation/installing-binary.html#multi-user-installation"
                echo ""
                echo " --no-daemon: Simple, single-user installation that does not require root and is"
                echo "              trivial to uninstall."
                echo "              (default)"
                echo ""
                echo " --yes:               Run the script non-interactively, accepting all prompts."
                echo ""
                echo " --no-channel-add:    Don't add any channels. nixpkgs-unstable is installed by default."
                echo ""
                echo " --no-modify-profile: Don't modify the user profile to automatically load nix."
                echo ""
                echo " --daemon-user-count: Number of build users to create. Defaults to 32."
                echo ""
                echo " --nix-extra-conf-file: Path to nix.conf to prepend when installing /etc/nix/nix.conf"
                echo ""
                if [ -n "${INVOKED_FROM_INSTALL_IN:-}" ]; then
                    echo " --tarball-url-prefix URL: Base URL to download the Nix tarball from."
                fi
            } >&2

            exit;;
    esac
    shift
done

if [ "$INSTALL_MODE" = "daemon" ]; then
    # Check for bash on systems that don't have it by default
    if [ "$OS" = "FreeBSD" ] && ! command -v bash >/dev/null 2>&1; then
        printf '\e[1;31mError: bash is required for multi-user installation but was not found.\e[0m\n' >&2
        printf 'Please install bash first:\n' >&2
        printf '  pkg install bash\n' >&2
        exit 1
    fi
    printf '\e[1;31mSwitching to the Multi-user Installer\e[0m\n'
    exec "$self/install-multi-user" $ACTION
    exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
    printf '\e[1;31mwarning: installing Nix as root is not supported by this script!\e[0m\n'
fi

echo "performing a single-user installation of Nix..." >&2

if ! [ -e "$dest" ]; then
    cmd="mkdir -m 0755 $dest && chown $USER $dest"
    echo "directory $dest does not exist; creating it by running '$cmd' using $NIX_BECOME" >&2
    if ! $NIX_BECOME sh -c "$cmd"; then
        echo "$0: please manually run '$cmd' as root to create $dest" >&2
        exit 1
    fi
fi

if ! [ -w "$dest" ]; then
    echo "$0: directory $dest exists, but is not writable by you. This could indicate that another user has already performed a single-user installation of Nix on this system. If you wish to enable multi-user support see https://nix.dev/manual/nix/stable/installation/multi-user.html. If you wish to continue with a single-user install for $USER please run 'chown -R $USER $dest' as root." >&2
    exit 1
fi

# The auto-chroot code in openFromNonUri() checks for the
# non-existence of /nix/var/nix, so we need to create it here.
mkdir -p "$dest/store" "$dest/var/nix"

printf "copying Nix to %s..." "${dest}/store" >&2
# Insert a newline if no progress is shown.
if [ ! -t 0 ]; then
  echo ""
fi

for i in $(cd "$self/store" >/dev/null && echo ./*); do
    if [ -t 0 ]; then
      printf "." >&2
    fi
    i_tmp="$dest/store/$i.$$"
    if [ -e "$i_tmp" ]; then
        rm -rf "$i_tmp"
    fi
    if ! [ -e "$dest/store/$i" ]; then
        if [ "$OS" = "Darwin" ] || [ "$OS" = "FreeBSD" ]; then
            cp -RPp "$self/store/$i" "$i_tmp"
        else
            cp -RP --preserve=ownership,timestamps "$self/store/$i" "$i_tmp"
        fi
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

# shellcheck source=./nix-profile.sh.in
. "$nix/etc/profile.d/nix.sh"

NIX_LINK="$HOME/.nix-profile"

if ! "$nix/bin/nix-env" -i "$nix"; then
    echo "$0: unable to install Nix into your default profile" >&2
    exit 1
fi

# Install an SSL certificate bundle.
if [ -z "$NIX_SSL_CERT_FILE" ] || ! [ -f "$NIX_SSL_CERT_FILE" ]; then
    "$nix/bin/nix-env" -i "$cacert"
    export NIX_SSL_CERT_FILE="$NIX_LINK/etc/ssl/certs/ca-bundle.crt"
fi

# Subscribe the user to the Nixpkgs channel and fetch it.
if [ -z "$NIX_INSTALLER_NO_CHANNEL_ADD" ]; then
    if ! "$nix/bin/nix-channel" --list | grep -q "^nixpkgs "; then
        "$nix/bin/nix-channel" --add https://channels.nixos.org/nixpkgs-unstable
    fi
    if [ -z "$_NIX_INSTALLER_TEST" ]; then
        if ! "$nix/bin/nix-channel" --update nixpkgs; then
            echo "Fetching the nixpkgs channel failed. (Are you offline?)"
            echo "To try again later, run \"nix-channel --update nixpkgs\"."
        fi
    fi
fi

added=
p=
p_sh=$NIX_LINK/etc/profile.d/nix.sh
p_fish=$NIX_LINK/etc/profile.d/nix.fish
if [ -z "$NIX_INSTALLER_NO_MODIFY_PROFILE" ]; then
    # Make the shell source nix.sh during login.
    for i in .bash_profile .bash_login .profile; do
        fn="$HOME/$i"
        if [ -w "$fn" ]; then
            if ! grep -q "$p_sh" "$fn"; then
                echo "modifying $fn..." >&2
                printf '\nif [ -e %s ]; then . %s; fi # added by Nix installer\n' "$p_sh" "$p_sh" >> "$fn"
            fi
            added=1
            p=${p_sh}
            break
        fi
    done
    for i in .zshenv .zshrc; do
        fn="$HOME/$i"
        if [ -w "$fn" ]; then
            if ! grep -q "$p_sh" "$fn"; then
                echo "modifying $fn..." >&2
                printf '\nif [ -e %s ]; then . %s; fi # added by Nix installer\n' "$p_sh" "$p_sh" >> "$fn"
            fi
            added=1
            p=${p_sh}
            break
        fi
    done

    if [ -d "$HOME/.config/fish" ]; then
        fishdir=$HOME/.config/fish/conf.d
        if [ ! -d "$fishdir" ]; then
            mkdir -p "$fishdir"
        fi

        fn="$fishdir/nix.fish"
        echo "placing $fn..." >&2
        printf '\nif test -e %s; . %s; end # added by Nix installer\n' "$p_fish" "$p_fish" > "$fn"
        added=1
        p=${p_fish}
    fi
else
    p=${p_sh}
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
