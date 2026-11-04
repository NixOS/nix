#!/usr/bin/env bash

source common.sh

rm -rf "$TEST_ROOT"/eval-out
nix eval --store dummy:// --write-to "$TEST_ROOT"/eval-out --expr '{ x = "foo" + "bar"; y = { z = "bla"; }; }'
[[ $(cat "$TEST_ROOT"/eval-out/x) = foobar ]]
[[ $(cat "$TEST_ROOT"/eval-out/y/z) = bla ]]

rm -rf "$TEST_ROOT"/eval-out
(! nix eval --store dummy:// --write-to "$TEST_ROOT"/eval-out --expr '{ "." = "bla"; }')
