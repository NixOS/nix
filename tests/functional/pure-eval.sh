#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

nix eval --expr 'assert 1 + 2 == 3; true'

[[ $(nix eval --impure --expr 'builtins.readFile ./pure-eval.sh') =~ clearStore ]]

missingImpureErrorMsg=$(! nix eval --expr 'builtins.readFile ./pure-eval.sh' 2>&1)

# shellcheck disable=SC1111
echo "$missingImpureErrorMsg" | grepQuiet -- --impure || \
    fail "The error message should mention the “--impure” flag to unblock users"

[[ $(nix eval --expr 'builtins.pathExists ./pure-eval.sh') == false ]] || \
    fail "Calling 'pathExists' on a non-authorised path should return false"

(! nix eval --expr builtins.currentTime)
(! nix eval --expr builtins.currentSystem)

(! nix-instantiate --pure-eval ./simple.nix)

[[ $(nix eval --impure --expr "(import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x") == 123 ]]
(! nix eval --expr "(import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x")
nix eval --expr "(import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; sha256 = \"$(nix hash file pure-eval.nix --type sha256)\"; })).x"

rm -rf "$TEST_ROOT"/eval-out
nix eval --store dummy:// --write-to "$TEST_ROOT"/eval-out --expr '{ x = "foo" + "bar"; y = { z = "bla"; }; }'
[[ $(cat "$TEST_ROOT"/eval-out/x) = foobar ]]
[[ $(cat "$TEST_ROOT"/eval-out/y/z) = bla ]]

rm -rf "$TEST_ROOT"/eval-out
(! nix eval --store dummy:// --write-to "$TEST_ROOT"/eval-out --expr '{ "." = "bla"; }')

# shellcheck disable=SC2088
(! nix eval --expr '~/foo')

expectStderr 0 nix eval --expr "/some/absolute/path" \
  | grepQuiet "/some/absolute/path"

expectStderr 0 nix eval --expr "/some/absolute/path" --impure \
  | grepQuiet "/some/absolute/path"

expectStderr 0 nix eval --expr "some/relative/path" \
  | grepQuiet "$PWD/some/relative/path"

expectStderr 0 nix eval --expr "some/relative/path" --impure \
  | grepQuiet "$PWD/some/relative/path"
