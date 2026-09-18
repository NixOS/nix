#!/usr/bin/env bash

source common.sh

# The flakes common.sh sets _NIX_TEST_BARF_ON_UNCACHEABLE=1, which makes reads
# of local source paths (outside flake inputs) error out. The whole point of
# this test is the non-flake path, so disable that guard for this test.
unset _NIX_TEST_BARF_ON_UNCACHEABLE

clearStoreIfPossible

# Test that `.#foo` falls back to evaluating `default.nix` when the directory
# contains a `default.nix` but no `flake.nix`. See NixOS/nix#1929.

# Copy the shared fixtures into TEST_HOME while our cwd is still the test
# directory, so the relative `..` paths resolve to tests/functional.
cp ../simple.nix ../simple.builder.sh "${config_nix}" "$TEST_HOME/"

cd "$TEST_HOME"

cat > default.nix <<EOF
with import ./config.nix;
{
  foo = mkDerivation { name = "foo-from-default"; builder = ./simple.builder.sh; PATH = ""; goodPath = path; };
  bar = mkDerivation { name = "bar-from-default"; builder = ./simple.builder.sh; PATH = ""; goodPath = path; };
  nested.baz = mkDerivation { name = "baz-from-default"; builder = ./simple.builder.sh; PATH = ""; goodPath = path; };
}
EOF

# 1. `.#foo` should fall back to default.nix.foo
out=$(nix eval --raw .#foo.name)
[[ $out == foo-from-default ]] || fail "expected .#foo.name to be 'foo-from-default', got '$out'"

# 2. A second attribute through the fragment.
out=$(nix eval --raw .#bar.name)
[[ $out == bar-from-default ]] || fail "expected .#bar.name to be 'bar-from-default', got '$out'"

# 3. Nested attribute paths through the fragment should work.
out=$(nix eval --raw .#nested.baz.name)
[[ $out == baz-from-default ]] || fail "expected .#nested.baz.name to be 'baz-from-default', got '$out'"

# 4. Absolute path with fragment.
out=$(nix eval --raw "$PWD#foo.name")
[[ $out == foo-from-default ]] || fail "expected \$PWD#foo.name to be 'foo-from-default', got '$out'"

# 5. `nix build .#foo` should build via default.nix.
nix build --no-link .#foo || fail "nix build .#foo via default.nix fallback should succeed"

# 6. When a flake.nix exists alongside, it should take precedence.
cat > flake.nix <<EOF
{
  outputs = { self }:
    let config = import ./config.nix; in {
      packages.$system.foo = config.mkDerivation {
        name = "foo-from-flake";
        builder = ./simple.builder.sh;
        PATH = "";
        goodPath = config.path;
      };
    };
}
EOF

out=$(nix eval --raw .#foo.name)
[[ $out == foo-from-flake ]] || fail "with flake.nix present, expected 'foo-from-flake', got '$out'"

rm flake.nix flake.lock 2>/dev/null || true

# 7. With `--pure-eval` explicitly set, the fallback must be skipped.
#    Since there is no flake.nix, flake parsing then runs and errors out.
#    This guarantees the fallback never silently disables a user-set pure mode.
expectStderr 1 nix eval --pure-eval --raw .#foo | grepQuietInverse "foo-from-default"

# 8. When the `flakes` experimental feature is not enabled, `.#foo` should
#    use `default.nix` even if a `flake.nix` happens to sit alongside it —
#    the flake.nix would be unusable anyway.
cat > flake.nix <<EOF
{
  outputs = { self }:
    let config = import ./config.nix; in {
      packages.$system.foo = config.mkDerivation {
        name = "foo-from-flake";
        builder = ./simple.builder.sh;
        PATH = "";
        goodPath = config.path;
      };
    };
}
EOF

out=$(nix --experimental-features 'nix-command' eval --raw .#foo.name)
[[ $out == foo-from-default ]] \
    || fail "with flakes feature disabled, expected 'foo-from-default', got '$out'"

rm flake.nix flake.lock 2>/dev/null || true

# 9. In a directory that has neither `flake.nix` nor `default.nix`, the error
#    should mention that `default.nix` was not found either — that's the
#    likely thing the user expected.
#    Place a `.git` sentinel inside the directory so `searchUpFor` does not
#    walk past it and find $TEST_HOME's `default.nix`.
mkdir -p empty-dir/.git
expectStderr 1 nix eval --raw ./empty-dir#foo 2>&1 | grepQuiet "contain a 'default\.nix'"
