#!/usr/bin/env bash

export _NIX_FORCE_HTTP=1

source ./common.sh

requireGit
TODO_NixOS

createFlake1

repoDir="$TEST_ROOT/repo"
mkdir -p "$repoDir"
echo "# foo" >> "$flake1Dir/flake.nix"
git -C "$flake1Dir" commit -a -m bla

cat > "$repoDir"/flake.nix <<EOF
{
  inputs.dep = {
    type = "git";
    url = "file://$flake1Dir";
  };
  outputs = inputs: rec {
    revs = assert inputs.dep.number == 123; inputs.dep.revCount;
  };
}
EOF

# This will do a non-shallow fetch.
[[ $(nix eval "path:$repoDir#revs") = 2 ]]

# This should re-use the existing non-shallow clone.
clearStore
mv "$flake1Dir" "$flake1Dir.moved"
[[ $(nix eval "path:$repoDir#revs") = 2 ]]
