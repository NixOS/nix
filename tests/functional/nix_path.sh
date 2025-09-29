#!/usr/bin/env bash

# Regression for https://github.com/NixOS/nix/issues/5998 and https://github.com/NixOS/nix/issues/5980

source common.sh

export NIX_PATH=non-existent=/non-existent/but-unused-anyways:by-absolute-path=$PWD:by-relative-path=.

nix-instantiate --eval -E '<by-absolute-path/simple.nix>' --restrict-eval
nix-instantiate --eval -E '<by-relative-path/simple.nix>' --restrict-eval

# Should ideally also test this, but there’s no pure way to do it, so just trust me that it works
# nix-instantiate --eval -E '<nixpkgs>' -I nixpkgs=channel:nixos-unstable --restrict-eval

[[ $(nix-instantiate --find-file by-absolute-path/simple.nix) = $PWD/simple.nix ]]
[[ $(nix-instantiate --find-file by-relative-path/simple.nix) = $PWD/simple.nix ]]

# this is the human-readable specification for the following test cases of interactions between various ways of specifying NIX_PATH.
# TODO: the actual tests are incomplete and too manual.
# there should be 43 of them, since the table has 9 rows and columns, and 2 interactions are meaningless
# ideally they would work off the table programmatically.
#
# | precedence             | hard-coded | nix-path in file | extra-nix-path in file | nix-path in env | extra-nix-path in env | NIX_PATH  | nix-path  | extra-nix-path  | -I              |
# |------------------------|------------|------------------|------------------------|-----------------|-----------------------|-----------|-----------|-----------------|-----------------|
# | hard-coded             | x          | ^override        | ^append                | ^override       | ^append               | ^override | ^override | ^append         | ^prepend        |
# | nix-path in file       |            | last wins        | ^append                | ^override       | ^append               | ^override | ^override | ^append         | ^prepend        |
# | extra-nix-path in file |            |                  | append in order        | ^override       | ^append               | ^override | ^override | ^append         | ^prepend        |
# | nix-path in env        |            |                  |                        | last wins       | ^append               | ^override | ^override | ^append         | ^prepend        |
# | extra-nix-path in env  |            |                  |                        |                 | append in order       | ^override | ^override | ^append         | ^prepend        |
# | NIX_PATH               |            |                  |                        |                 |                       | x         | ^override | ^append         | ^prepend        |
# | nix-path               |            |                  |                        |                 |                       |           | last wins | ^append         | ^prepend        |
# | extra-nix-path         |            |                  |                        |                 |                       |           |           | append in order | append in order |
# | -I                     |            |                  |                        |                 |                       |           |           |                 | append in order |

unset NIX_PATH

mkdir -p "$TEST_ROOT"/{from-nix-path-file,from-NIX_PATH,from-nix-path,from-extra-nix-path,from-I}
for i in from-nix-path-file from-NIX_PATH from-nix-path from-extra-nix-path from-I; do
    touch "$TEST_ROOT"/$i/only-$i.nix
done

# finding something that's not in any of the default paths fails
# shellcheck disable=SC2091
( ! $(nix-instantiate --find-file test) )

echo "nix-path = test=$TEST_ROOT/from-nix-path-file" >> "$test_nix_conf"

# Use nix.conf in absence of NIX_PATH
[[ $(nix-instantiate --find-file test) = $TEST_ROOT/from-nix-path-file ]]

# NIX_PATH overrides nix.conf
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate --find-file test) = $TEST_ROOT/from-NIX_PATH ]]
# if NIX_PATH does not have the desired entry, it fails
(! NIX_PATH=test=$TEST_ROOT nix-instantiate --find-file test/only-from-nix-path-file.nix)

# -I extends nix.conf
[[ $(nix-instantiate -I test="$TEST_ROOT"/from-I --find-file test/only-from-I.nix) = $TEST_ROOT/from-I/only-from-I.nix ]]
# if -I does not have the desired entry, the value from nix.conf is used
[[ $(nix-instantiate -I test="$TEST_ROOT"/from-I --find-file test/only-from-nix-path-file.nix) = $TEST_ROOT/from-nix-path-file/only-from-nix-path-file.nix ]]

# -I extends NIX_PATH
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate -I test="$TEST_ROOT"/from-I --find-file test/only-from-I.nix) = $TEST_ROOT/from-I/only-from-I.nix ]]
# -I takes precedence over NIX_PATH
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate -I test="$TEST_ROOT"/from-I --find-file test) = $TEST_ROOT/from-I ]]
# if -I does not have the desired entry, the value from NIX_PATH is used
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate -I test="$TEST_ROOT"/from-I --find-file test/only-from-NIX_PATH.nix) = $TEST_ROOT/from-NIX_PATH/only-from-NIX_PATH.nix ]]

# --extra-nix-path extends NIX_PATH
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate --extra-nix-path test="$TEST_ROOT"/from-extra-nix-path --find-file test/only-from-extra-nix-path.nix) = $TEST_ROOT/from-extra-nix-path/only-from-extra-nix-path.nix ]]
# if --extra-nix-path does not have the desired entry, the value from NIX_PATH is used
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate --extra-nix-path test="$TEST_ROOT"/from-extra-nix-path --find-file test/only-from-NIX_PATH.nix) = $TEST_ROOT/from-NIX_PATH/only-from-NIX_PATH.nix ]]

# --nix-path overrides NIX_PATH
[[ $(NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate --nix-path test="$TEST_ROOT"/from-nix-path --find-file test) = $TEST_ROOT/from-nix-path ]]
# if --nix-path does not have the desired entry, it fails
(! NIX_PATH=test=$TEST_ROOT/from-NIX_PATH nix-instantiate --nix-path test="$TEST_ROOT"/from-nix-path --find-file test/only-from-NIX_PATH.nix)

# --nix-path overrides nix.conf
[[ $(nix-instantiate --nix-path test="$TEST_ROOT"/from-nix-path --find-file test) = $TEST_ROOT/from-nix-path ]]
(! nix-instantiate --nix-path test="$TEST_ROOT"/from-nix-path --find-file test/only-from-nix-path-file.nix)

# --extra-nix-path extends nix.conf
[[ $(nix-instantiate --extra-nix-path test="$TEST_ROOT"/from-extra-nix-path --find-file test/only-from-extra-nix-path.nix) = $TEST_ROOT/from-extra-nix-path/only-from-extra-nix-path.nix ]]
# if --extra-nix-path does not have the desired entry, it is taken from nix.conf
[[ $(nix-instantiate --extra-nix-path test="$TEST_ROOT"/from-extra-nix-path --find-file test) = $TEST_ROOT/from-nix-path-file ]]

# -I extends --nix-path
[[ $(nix-instantiate --nix-path test="$TEST_ROOT"/from-nix-path -I test="$TEST_ROOT"/from-I --find-file test/only-from-I.nix) = $TEST_ROOT/from-I/only-from-I.nix ]]
[[ $(nix-instantiate --nix-path test="$TEST_ROOT"/from-nix-path -I test="$TEST_ROOT"/from-I --find-file test/only-from-nix-path.nix) = $TEST_ROOT/from-nix-path/only-from-nix-path.nix ]]
