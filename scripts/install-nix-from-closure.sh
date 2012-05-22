#! /bin/sh -e

nix=@nix@
regInfo=@regInfo@

if ! $nix/bin/nix-store --load-db < $regInfo; then
    echo "$0: unable to register valid paths"
    exit 1
fi

. @nix@/etc/profile.d/nix.sh

if ! $nix/bin/nix-env -i @nix@; then
    echo "$0: unable to install Nix into your default profile"
    exit 1
fi

# Add nix.sh to the shell's profile.d directory.
p=$NIX_LINK/etc/profile.d/nix.sh

if [ -w /etc/profile.d ]; then
    ln -s $p /etc/profile.d/
elif [ -w /usr/local/etc/profile.d ]; then
    ln -s $p /usr/local/etc/profile.d/
else
    cat <<EOF
Installation finished.  To ensure that the necessary environment
variables are set, please add the line

  source $p

to your shell profile (e.g. ~/.profile).
EOF
fi