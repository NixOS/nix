#!/usr/bin/env bash

source ./common.sh

requireGit

# Exercise the remote-fetch cache path even though the fixture uses file://.
export _NIX_FORCE_HTTP=1
export XDG_CACHE_HOME="$TEST_ROOT/cache"

inputDir="$TEST_ROOT/input"
rootDir="$TEST_ROOT/root"

createGitRepo "$inputDir"
mkdir -p "$rootDir"

cat > "$inputDir/flake.nix" <<'EOF'
{
  outputs = { self }: {
    contents = builtins.readFile ./data;
  };
}
EOF

echo exact-rev > "$inputDir/data"
git -C "$inputDir" add flake.nix data
git -C "$inputDir" commit -m 'Initial'
rev=$(git -C "$inputDir" rev-parse HEAD)

cat > "$rootDir/flake.nix" <<EOF
{
  inputs = {
    revFlake.url = "git+file://$inputDir?rev=$rev";
    revNonFlake = {
      url = "git+file://$inputDir?rev=$rev";
      flake = false;
    };
    explicitRef = {
      url = "git+file://$inputDir?ref=refs/heads/master&rev=$rev";
      flake = false;
    };
  };

  outputs = { self, revFlake, revNonFlake, explicitRef }: {
    flakeContents = revFlake.contents;
    nonFlakeContents = builtins.readFile (revNonFlake + "/data");
    explicitRefContents = builtins.readFile (explicitRef + "/data");
  };
}
EOF

nix flake lock "$rootDir"
cp "$rootDir/flake.lock" "$rootDir/flake.lock.generated"

# Rev-only flake and non-flake inputs lock as HEAD and select the same tree.
# Resolution must preserve the original attributes and any explicitly supplied ref.
jq -e --arg rev "$rev" '
  .nodes.revFlake as $revFlake
  | .nodes.revNonFlake as $revNonFlake
  | .nodes.explicitRef as $explicitRef
  | ($revFlake.locked.ref == "HEAD")
    and ($revNonFlake.locked.ref == "HEAD")
    and ($revFlake.locked.rev == $rev)
    and ($revNonFlake.locked.rev == $rev)
    and ($revFlake.locked.narHash == $revNonFlake.locked.narHash)
    and ($revFlake.locked.narHash | type == "string" and length > 0)
    and ($revFlake.original.rev == $rev)
    and ($revNonFlake.original.rev == $rev)
    and ($revFlake.original | has("ref") | not)
    and ($revNonFlake.original | has("ref") | not)
    and ($explicitRef.locked.ref == "refs/heads/master")
    and ($explicitRef.locked.rev == $rev)
    and ($explicitRef.original.ref == "refs/heads/master")
' "$rootDir/flake.lock"

generatedFlakeContents=$(nix eval --raw "$rootDir#flakeContents")
generatedNonFlakeContents=$(nix eval --raw "$rootDir#nonFlakeContents")
generatedExplicitRefContents=$(nix eval --raw "$rootDir#explicitRefContents")

[[ $generatedFlakeContents == exact-rev ]]
[[ $generatedNonFlakeContents == "$generatedFlakeContents" ]]
[[ $generatedExplicitRefContents == "$generatedFlakeContents" ]]

# Evaluating and locking again must leave the generated lockfile unchanged.
nix flake lock "$rootDir"
cmp "$rootDir/flake.lock" "$rootDir/flake.lock.generated"

# Simulate an older lockfile that resolved rev-only inputs to a concrete ref.
# Reusing it must preserve both its contents and its recorded refs.
jq '(.nodes.revFlake.locked.ref, .nodes.revNonFlake.locked.ref) = "refs/heads/master"' \
  "$rootDir/flake.lock" > "$rootDir/flake.lock.old"
mv "$rootDir/flake.lock.old" "$rootDir/flake.lock"
cp "$rootDir/flake.lock" "$rootDir/flake.lock.concrete-ref"

[[ $(nix eval --raw "$rootDir#flakeContents") == "$generatedFlakeContents" ]]
[[ $(nix eval --raw "$rootDir#nonFlakeContents") == "$generatedNonFlakeContents" ]]
[[ $(nix eval --raw "$rootDir#explicitRefContents") == "$generatedExplicitRefContents" ]]

nix flake lock "$rootDir"
cmp "$rootDir/flake.lock" "$rootDir/flake.lock.concrete-ref"
