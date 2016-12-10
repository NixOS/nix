#!/usr/bin/env bash
#
# Tests to be run during PRs
set -euo pipefail
cd "$(dirname "$0")/.."

echo "--- Unshallow"

# Travis-CI only does a shallow copy.
# release.nix is using fetchGit which depends on a full history.
git pull --unshallow

echo "--- Testing build"

nix-build release.nix -A coverage

if [[ $(uname) = Darwin ]]; then
  echo "--- Testing installation"

  ./tests/install-darwin.sh
fi
