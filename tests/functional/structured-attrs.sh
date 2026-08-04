#!/usr/bin/env bash

source common.sh

# https://github.com/NixOS/nix/pull/14189
requireDaemonNewerThan "2.33"

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
# (which is an associative array in this case) being fine.
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
expectStderr 0 nix-instantiate --expr "$hackyExpr" --eval --strict | grepQuiet "setting structured attributes via '__json' is deprecated, and may be disallowed in future versions of Nix. Set '__structuredAttrs = true' instead."

# Check it works with the expected structured attrs
hacky=$(nix-instantiate --expr "$hackyExpr")
nix derivation show "$hacky" | jq --exit-status '.derivations."'"$(basename "$hacky")"'".structuredAttrs | . == {"a": 1}'

# Quirk:
# A path reached through a `__toString` call inside `__structuredAttrs` must
# NOT be copied to the store: the JSON keeps the path in its raw string form
# and the derivation gains no `srcs` reference. Any change that starts copying
# would alter the drv hash and add a dependency to every derivation using this
# pattern.
#
# This is the unfortunate consequence of a lack of proper `__toString` return
# value checking in previous releases.
# We could emit a warning about this, but the benefit would be marginal.
#
# This behavior is due to any `__toString` regardless of `outPath` attributes,
# so check both nesting orders.
toStringPathDir=$TEST_ROOT/toString-path-src
mkdir -p "$toStringPathDir"
for myPath in \
    "{ __toString = self: $toStringPathDir; }" \
    "{ outPath = { __toString = self: $toStringPathDir; }; }" \
    "{ __toString = self: { outPath = $toStringPathDir; }; }"; do
    drv=$(nix-instantiate --expr "
      derivation {
        name = \"toString-path\";
        system = \"foo\";
        builder = \"/bin/sh\";
        __structuredAttrs = true;
        myPath = $myPath;
      }
    ")
    nix derivation show "$drv" | jq --exit-status \
        --arg raw "$toStringPathDir" \
        '.derivations[].structuredAttrs.myPath == $raw'
    nix derivation show "$drv" | jq --exit-status \
        '.derivations[].inputs.srcs == []'
done

if isDaemonNewer "2.34pre"; then
    # Test warning for non-object exportReferencesGraph in structured attrs
    # shellcheck disable=SC2016
    expectStderr 0 nix-build --no-out-link --expr '
    with import ./config.nix;
    mkDerivation {
        name = "export-graph-non-object";
        __structuredAttrs = true;
        exportReferencesGraph = [ "foo" "bar" ];
        builder = "/bin/sh";
        args = ["-c" "echo foo > ${builtins.placeholder "out"}"];
    }
    ' | grepQuiet "warning:.*exportReferencesGraph.*not a JSON object"
fi
