#! /usr/bin/env bash
set -e

export NIX_CURL_FLAGS=-sS

if [[ $1 == nix ]]; then
    echo "=== Installing Nix..."
    # Install Nix
    bash <(curl -sS https://nixos.org/nix/install)
    source $HOME/.nix-profile/etc/profile.d/nix.sh

    # Make sure we can use hydra's binary cache
    sudo mkdir /etc/nix
    sudo tee /etc/nix/nix.conf <<EOF >/dev/null
binary-caches = http://cache.nixos.org http://hydra.nixos.org
trusted-binary-caches = http://hydra.nixos.org
build-max-jobs = 4
EOF

elif [[ $1 == install ]]; then
  source $HOME/.nix-profile/etc/profile.d/nix.sh
  ./dev-shell --run "./bootstrap.sh; configurePhase; make install"
elif [[ $1 == check ]]; then
  source $HOME/.nix-profile/etc/profile.d/nix.sh
  ./dev-shell --run "make installcheck"
else
    echo "$0: Unknown option $1" >&2
    false
fi