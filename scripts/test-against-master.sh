#!/usr/bin/env bash

set -euo pipefail
set -x;

CurrentNixDir=$(pwd)
CurrentRev=${GITHUB_SHA:-$(git rev-parse HEAD)}
WorkDir=$(mktemp -d)
trap 'rm -r "$WorkDir"' EXIT
GITHUB_TOKEN_OPTION=()
if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    GITHUB_TOKEN_OPTION=("--option" "access-tokens" "github.com=$GITHUB_TOKEN")
fi

pushd "$WorkDir"

cat <<EOF > flake.nix
{
    inputs.currentNix.url = "git+file://$CurrentNixDir?rev=$CurrentRev";
    inputs.nixMaster.url = "github:nixos/nix";

    outputs = { self, currentNix, nixMaster }: {
        checks = builtins.mapAttrs (systemName: _:
        { againstMaster = currentNix.lib.testAgainst.\${systemName} nixMaster.defaultPackage.\${systemName}; }
        ) currentNix.defaultPackage;
    };
}
EOF
nix flake check\
    --experimental-features 'nix-command flakes' \
    "${GITHUB_TOKEN_OPTION[@]}"

popd
