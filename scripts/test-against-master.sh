#!/usr/bin/env bash

set -euo pipefail
set -x;

CurrentNixDir=$(pwd)
CurrentRev=${GITHUB_SHA:-$(git rev-parse HEAD)}
WorkDir=$(mktemp -d)
trap 'rm -r "$WorkDir"' EXIT

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
nix --experimental-features 'nix-command flakes' flake check

popd
