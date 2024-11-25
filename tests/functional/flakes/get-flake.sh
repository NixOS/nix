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
