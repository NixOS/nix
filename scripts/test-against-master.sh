#!/usr/bin/env bash

set -euo pipefail
set -x;

CurrentNixDir=$(pwd)
CurrentRev=${GITHUB_SHA:-$(git rev-parse HEAD)}
WorkDir=$(mktemp -d)
trap 'rm -rf "$WorkDir"' EXIT
GITHUB_TOKEN_OPTION=()
if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    GITHUB_TOKEN_OPTION=("--option" "access-tokens" "github.com=$GITHUB_TOKEN")
fi

pushd "$WorkDir"

git clone "https://github.com/nixos/nix"

cat <<EOF > default.nix
let
  currentNix = import $CurrentNixDir;
  masterNix = import ./nix;
in
currentNix.lib.testAgainst.\${builtins.currentSystem} masterNix.defaultPackage.\${builtins.currentSystem}
EOF

nix-build "${GITHUB_TOKEN_OPTION[@]}"

popd
