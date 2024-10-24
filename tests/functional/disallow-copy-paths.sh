#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# shellcheck disable=SC2016
path="$(nix eval --raw --impure --expr '"${./disallow-copy-paths.sh}"')"

# shellcheck disable=SC2016
expectStderr 1 nix-instantiate \
  --disallow-copy-paths "$path" \
  --expr --strict \
  --argstr path "$path" \
  '{ path }: "${/. + path}" + "bla bla"' \
  | grepQuiet "error.*not allowed to copy.*$path.* due to option.*disallow-copy-paths"
