#!/usr/bin/env bash

if ! type -p pre-commit &>/dev/null; then
  echo "format.sh: pre-commit not found. Please use \`nix develop -c ./maintainers/format.sh\`.";
  exit 1;
fi;
if test -z "$_NIX_PRE_COMMIT_HOOKS_CONFIG"; then
  echo "format.sh: _NIX_PRE_COMMIT_HOOKS_CONFIG not set. Please use \`nix develop -c ./maintainers/format.sh\`.";
  exit 1;
fi;

while ! pre-commit run --config "$_NIX_PRE_COMMIT_HOOKS_CONFIG" --all-files; do
    if [ "${1:-}" != "--until-stable" ]; then
        exit 1
    fi
done
