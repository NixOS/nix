#!/usr/bin/env bash

source ./common.sh

TODO_NixOS
enableFeatures "build-time-fetch-tree"
restartDaemon
requireGit

lazy="$TEST_ROOT/lazy"
createGitRepo "$lazy"
echo world > "$lazy/who"
git -C "$lazy" add who
git -C "$lazy" commit -a -m foo

repo="$TEST_ROOT/repo"

createGitRepo "$repo"

cat > "$repo/flake.nix" <<EOF
{
  inputs.lazy = {
    type = "git";
    url = "file://$lazy";
    flake = false;
    buildTime = true;
  };

  outputs = { self, lazy }: {
    packages.$system.default = (import ./config.nix).mkDerivation {
      name = "foo";
      buildCommand = "cat \${lazy}/who > \$out";
    };
  };
}
EOF

cp "${config_nix}" "$repo/"
git -C "$repo" add flake.nix config.nix
nix flake lock "$repo"
git -C "$repo" add flake.lock
git -C "$repo" commit -a -m foo

clearStore

nix build --out-link "$TEST_ROOT/result" -L "$repo"
[[ $(cat "$TEST_ROOT/result") = world ]]

echo utrecht > "$lazy/who"
git -C "$lazy" commit -a -m foo

nix flake update --flake "$repo"

clearStore

nix build --out-link "$TEST_ROOT/result" -L "$repo"
[[ $(cat "$TEST_ROOT/result") = utrecht ]]

rm -rf "$lazy"

clearStore

expectStderr 1 nix build --out-link "$TEST_ROOT/result" -L "$repo" | grepQuiet "Cannot build.*source.drv"
