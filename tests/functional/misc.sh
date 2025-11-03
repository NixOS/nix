#!/usr/bin/env bash

source common.sh

# Tests miscellaneous commands.

# Do all commands have help?
#nix-env --help | grepQuiet install
#nix-store --help | grepQuiet realise
#nix-instantiate --help | grepQuiet eval
#nix-hash --help | grepQuiet base32

# Can we ask for the version number?
nix-env --version | grep -F "${_NIX_TEST_CLIENT_VERSION:-$version}"

nix_env=$(type -P nix-env)
# shellcheck disable=SC2123
(PATH=""; ! $nix_env --help 2>&1 ) | grepQuiet -F "The 'man' command was not found, but it is needed for 'nix-env' and some other 'nix-*' commands' help text. Perhaps you could install the 'man' command?"

# Usage errors.
expect 1 nix-env --foo 2>&1 | grep "no operation"
expect 1 nix-env -q --foo 2>&1 | grep "unknown flag"

# Eval Errors.
eval_arg_res=$(nix-instantiate --eval -E 'let a = {} // a; in a.foo' 2>&1 || true)
echo "$eval_arg_res" | grep "at «string»:1:15:"
echo "$eval_arg_res" | grep "infinite recursion encountered"

eval_stdin_res=$(echo 'let a = {} // a; in a.foo' | nix-instantiate --eval -E - 2>&1 || true)
echo "$eval_stdin_res" | grep "at «stdin»:1:15:"
echo "$eval_stdin_res" | grep "infinite recursion encountered"

# Attribute path errors
expectStderr 1 nix-instantiate --eval -E '{}' -A '"x' | grepQuiet "missing closing quote in selection path"
expectStderr 1 nix-instantiate --eval -E '[]' -A 'x' | grepQuiet "should be a set"
expectStderr 1 nix-instantiate --eval -E '{}' -A '1' | grepQuiet "should be a list"
expectStderr 1 nix-instantiate --eval -E '{}' -A '.' | grepQuiet "empty attribute name"
expectStderr 1 nix-instantiate --eval -E '[]' -A '1' | grepQuiet "out of range"

# Unknown setting warning
# NOTE(cole-h): behavior is different depending on the order, which is why we test an unknown option
# before and after the `'{}'`!
out="$(expectStderr 0 nix-instantiate --option foobar baz --expr '{}')"
[[ "$(echo "$out" | grep -c foobar )" = 1 ]]

out="$(expectStderr 0 nix-instantiate '{}' --option foobar baz --expr )"
[[ "$(echo "$out" | grep -c foobar )" = 1 ]]

if [[ $(uname) = Linux && $(uname -m) = i686 ]]; then
    [[ $(nix config show system) = i686-linux ]]
fi
