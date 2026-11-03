#!/usr/bin/env bash

source common.sh

before=$(nix-build diff-closures.nix -A before --no-out-link)
after=$(nix-build diff-closures.nix -A after --no-out-link)

json=$(nix store diff-closures --json "$before" "$after")

jq -e '
  .version == 1
  and (.packages | keys == ["foo", "sized"])
  and .packages.foo.versionsBefore == ["1.0"]
  and .packages.foo.versionsAfter == ["2.0"]
  and (.packages.foo.sizeDelta | type == "number")
  and .packages.sized.versionsBefore == []
  and .packages.sized.versionsAfter == []
  and .packages.sized.sizeDelta > 8192
' <<< "$json"

nix store diff-closures --json "$before" "$before" | jq -e '
  .version == 1
  and .packages == {}
'

text=$(nix store diff-closures "$before" "$after")
grepQuiet 'foo: 1.0 → 2.0' <<< "$text"
grepQuiet 'sized:' <<< "$text"
