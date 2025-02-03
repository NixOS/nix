#!/usr/bin/env bash

source common.sh

# Store layer needs bugfix
requireDaemonNewerThan "2.27pre20250122"

nix-build ../fixed.nix -A git --no-out-link
