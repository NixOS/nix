#!/usr/bin/env bash

source ./common.sh

TODO_NixOS

createFlake1
createFlake2

# 'getFlake' on an unlocked flakeref should fail in pure mode, but
# succeed in impure mode.
(! nix build -o "$TEST_ROOT/result" --expr "(builtins.getFlake \"$flake1Dir\").packages.$system.default")
nix build -o "$TEST_ROOT/result" --expr "(builtins.getFlake \"$flake1Dir\").packages.$system.default" --impure

# 'getFlake' on a locked flakeref should succeed even in pure mode.
hash=$(nix flake metadata flake1 --json --refresh | jq -r .revision)
nix build -o "$TEST_ROOT/result" --expr "(builtins.getFlake \"git+file://$flake1Dir?rev=$hash\").packages.$system.default"

# Building a flake with an unlocked dependency should fail in pure mode.
(! nix eval --expr "builtins.getFlake \"$flake2Dir\"")

# But should succeed in impure mode.
nix eval --expr "builtins.getFlake \"$flake2Dir\"" --impure

# Test overrides in getFlake.
flake1Copy="$flake1Dir-copy"
rm -rf "$flake1Copy"
cp -r "$flake1Dir" "$flake1Copy"
sed -i "$flake1Copy/simple.builder.sh" -e 's/World/Universe/'

# Should fail in pure mode since the override is unlocked.
(! nix build -o "$TEST_ROOT/result" --expr "(builtins.getFlake { url = \"$flake2Dir\"; inputs.flake1.url = \"$flake1Copy\"; }).packages.$system.bar")

# Should succeed in impure mode.
nix build -o "$TEST_ROOT/result" --expr "(builtins.getFlake { url = \"$flake2Dir\"; inputs.flake1.url = \"$flake1Copy\"; }).packages.$system.bar" --impure
[[ $(cat "$TEST_ROOT/result/hello") = 'Hello Universe!' ]]

# Should succeed if we lock the override.
git -C "$flake1Copy" commit -a -m 'bla'

flake1CopyLocked="$(nix flake metadata --json "$flake1Copy" | jq -r .url)"

nix build -o "$TEST_ROOT/result" --expr "(builtins.getFlake { url = \"$flake2Dir\"; inputs.flake1.url = \"$flake1CopyLocked\"; }).packages.$system.bar"
