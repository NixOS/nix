#!/usr/bin/env bash

source common.sh

# Store layer needs bugfix
requireDaemonNewerThan "2.27pre20250122"

nix-build ../fixed.nix -A git-sha1 --no-out-link

if isDaemonNewer "2.31pre20250724"; then
    nix-build ../fixed.nix -A git-sha256 --no-out-link
fi
