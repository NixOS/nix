source ./common.sh

requireGit

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

flake1Dir="$TEST_ROOT/eval-cache-flake"
flake2Dir="$TEST_ROOT/flake2"
createGitRepo "$flake1Dir" ""
createGitRepo "$flake2Dir" ""

createSimpleGitFlake "$flake2Dir"

nix registry add --registry "$registry" flake2 "git+file://$flake2Dir"

cat >"$flake1Dir/flake.nix" <<EOF
{
  description = "Fnord";
  outputs = { self, flake2 }: {
    foo.bar = throw "breaks";
  };
}
EOF

git -C "$flake1Dir" add flake.nix
nix flake lock "$flake2Dir"
nix flake lock "$flake1Dir"

# FIXME `packages.$system.foo` requires three invocations of `nix build`,
# for `foo = throw` only it'll never be cached.
expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: breaks'
expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: cached failure of attribute '"'foo.bar'"

git -C "$flake1Dir" add flake.lock
git -C "$flake1Dir" commit -m "Init"

# Cache is invalidated with a new git revision
expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: breaks'
expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: cached failure of attribute '"'foo.bar'"
