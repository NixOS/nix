#!/usr/bin/env bash

source common.sh

# https://github.com/NixOS/nix/pull/14189
requireDaemonNewerThan "2.33"

clearStoreIfPossible

rm -f "$TEST_ROOT"/result

nix-build structured-attrs.nix -A all -o "$TEST_ROOT"/result

[[ $(cat "$TEST_ROOT"/result/foo) = bar ]]
[[ $(cat "$TEST_ROOT"/result-dev/foo) = foo ]]

export NIX_BUILD_SHELL=$SHELL
# shellcheck disable=SC2016
env NIX_PATH=nixpkgs=shell.nix nix-shell structured-attrs-shell.nix \
    --run 'test "3" = "$(jq ".my.list|length" < $NIX_ATTRS_JSON_FILE)"'

# shellcheck disable=SC2016
nix develop -f structured-attrs-shell.nix -c bash -c 'test "3" = "$(jq ".my.list|length" < $NIX_ATTRS_JSON_FILE)"'

TODO_NixOS # following line fails.

# `nix develop` is a slightly special way of dealing with environment vars, it parses
# these from a shell-file exported from a derivation. This is to test especially `outputs`
# (which is an associative array in thsi case) being fine.
# shellcheck disable=SC2016
nix develop -f structured-attrs-shell.nix -c bash -c 'test -n "$out"'

nix print-dev-env -f structured-attrs-shell.nix | grepQuiet 'NIX_ATTRS_JSON_FILE='
nix print-dev-env -f structured-attrs-shell.nix | grepQuiet 'NIX_ATTRS_SH_FILE='
nix print-dev-env -f shell.nix shellDrv | grepQuietInverse 'NIX_ATTRS_SH_FILE'

jsonOut="$(nix print-dev-env -f structured-attrs-shell.nix --json)"

test "$(<<<"$jsonOut" jq '.structuredAttrs|keys|.[]' -r)" = "$(printf ".attrs.json\n.attrs.sh")"

test "$(<<<"$jsonOut" jq '.variables.outputs.value.out' -r)" = "$(<<<"$jsonOut" jq '.structuredAttrs.".attrs.json"' -r | jq -r '.outputs.out')"

# Hacky way of making structured attrs. We should preserve for now for back compat, but also deprecate.

hackyExpr='derivation { name = "a"; system = "foo"; builder = "/bin/sh"; __json = builtins.toJSON { a = 1; }; }'

# Check for deprecation message
expectStderr 0 nix-instantiate --expr "$hackyExpr" --eval --strict | grepQuiet "In derivation 'a': setting structured attributes via '__json' is deprecated, and may be disallowed in future versions of Nix. Set '__structuredAttrs = true' instead."

# Check it works with the expected structured attrs
hacky=$(nix-instantiate --expr "$hackyExpr")
nix derivation show "$hacky" | jq --exit-status '."'"$(basename "$hacky")"'".structuredAttrs | . == {"a": 1}'
