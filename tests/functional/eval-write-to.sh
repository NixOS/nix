#!/usr/bin/env bash

source common.sh

nix eval --write-to "$TEST_ROOT"/basic --expr '{ x = "foo" + "bar"; y = { z = "bla"; }; }'
[[ $(cat "$TEST_ROOT"/basic/x) = foobar ]]
[[ $(cat "$TEST_ROOT"/basic/y/z) = bla ]]

expect 1 nix eval --write-to "$TEST_ROOT"/dot --expr '{ "." = "bla"; }'

# Names must be single path components.
expectStderr 1 nix eval --write-to "$TEST_ROOT"/slash --expr '{ "a/b" = "bla"; }' \
  | grepQuiet "invalid file name 'a/b'"

# Nothing existing is overwritten, neither directories nor files.
mkdir "$TEST_ROOT"/existing-dir
expect 1 nix eval --write-to "$TEST_ROOT"/existing-dir --expr '{ x = "bla"; }'
[[ ! -e "$TEST_ROOT"/existing-dir/x ]]
echo old > "$TEST_ROOT"/existing-file
expect 1 nix eval --write-to "$TEST_ROOT"/existing-file --expr '"new"'
[[ $(cat "$TEST_ROOT"/existing-file) = old ]]

# As with POSIX, a trailing slash is fine for a directory, but not for a file.
nix eval --write-to "$TEST_ROOT"/dir-slash/ --expr '{ x = "bla"; }'
[[ $(cat "$TEST_ROOT"/dir-slash/x) = bla ]]
nix eval --write-to "$TEST_ROOT"/dir-slashes// --expr '{ x = "bla"; }'
[[ $(cat "$TEST_ROOT"/dir-slashes/x) = bla ]]
expectStderr 1 nix eval --write-to "$TEST_ROOT"/file-slash/ --expr '"bla"' \
  | grepQuiet "Is a directory"
[[ ! -e "$TEST_ROOT"/file-slash ]]

# Symlinks in the parent of the destination are followed.
mkdir "$TEST_ROOT"/symlink-target
ln -s symlink-target "$TEST_ROOT"/symlink
nix eval --write-to "$TEST_ROOT"/symlink/out --expr '{ x = "bla"; }'
[[ $(cat "$TEST_ROOT"/symlink-target/out/x) = bla ]]

# But a symlink as the destination itself is not followed, whether or not
# its target exists.
mkdir "$TEST_ROOT"/trailing-symlink-target
ln -s trailing-symlink-target "$TEST_ROOT"/trailing-symlink
expect 1 nix eval --write-to "$TEST_ROOT"/trailing-symlink --expr '{ x = "bla"; }'
[[ ! -e "$TEST_ROOT"/trailing-symlink-target/x ]]
ln -s dangling-symlink-target "$TEST_ROOT"/dangling-symlink
expect 1 nix eval --write-to "$TEST_ROOT"/dangling-symlink --expr '"bla"'
[[ ! -e "$TEST_ROOT"/dangling-symlink-target ]]

# Nesting is bounded like it is for NARs, to a depth of 64.
nestedExpr='n: let f = n: if n == 0 then "bla" else { a = f (n - 1); }; in f n'
nix eval --write-to "$TEST_ROOT"/depth-63 --expr "($nestedExpr) 63"
expectStderr 1 nix eval --write-to "$TEST_ROOT"/depth-64 --expr "($nestedExpr) 64" \
  | grepQuiet "exceeds maximum depth of 64"
