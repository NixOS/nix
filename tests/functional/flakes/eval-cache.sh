#!/usr/bin/env bash

source ./common.sh

requireGit

# Test basic caching: trace should only appear on first evaluation
basicCacheDir="$TEST_ROOT/basic-cache-flake"
createGitRepo "$basicCacheDir" ""
cp "${config_nix}" "$basicCacheDir/"
git -C "$basicCacheDir" add config.nix
git -C "$basicCacheDir" commit -m "config.nix"

cat >"$basicCacheDir/flake.nix" <<EOF
{
  description = "Basic cache test";
  outputs = { self }: let inherit (import ./config.nix) mkDerivation; in {
    # Assert pure-eval is enabled (builtins.currentSystem is unavailable in pure mode)
    packages.$system.default = assert builtins ? currentSystem -> throw "pure-eval not enabled";
      builtins.trace "basic-test-evaluating" (mkDerivation {
        name = "cached-build";
        buildCommand = "echo hello > \\\$out";
      });
  };
}
EOF

git -C "$basicCacheDir" add flake.nix
git -C "$basicCacheDir" commit -m "Init"

clearCache

# First build should show trace
nix build --no-link "$basicCacheDir" 2>&1 | grepQuiet 'trace: basic-test-evaluating'

# Second build should use cache, no trace output
nix build --no-link "$basicCacheDir" 2>&1 | grepQuietInverse 'trace: basic-test-evaluating'

# Third build should also use cache, no trace output
nix build --no-link "$basicCacheDir" 2>&1 | grepQuietInverse 'trace: basic-test-evaluating'

# Test edge cases with separate flake
flake1Dir="$TEST_ROOT/eval-cache-flake"

createGitRepo "$flake1Dir" ""
cp ../simple.nix ../simple.builder.sh "${config_nix}" "$flake1Dir/"
git -C "$flake1Dir" add simple.nix simple.builder.sh config.nix
git -C "$flake1Dir" commit -m "config.nix"

cat >"$flake1Dir/flake.nix" <<EOF
{
  description = "Fnord";
  outputs = { self }: let inherit (import ./config.nix) mkDerivation; in {
    foo.bar = throw "breaks";
    drv = mkDerivation {
      name = "build";
      buildCommand = ''
        echo true > \$out
      '';
    };
    stack-depth =
      let
        f = x: if x == 0 then true else f (x - 1);
      in
        assert (f 100); self.drv;
    ifd = assert (import self.drv); self.drv;
  };
}
EOF

git -C "$flake1Dir" add flake.nix
git -C "$flake1Dir" commit -m "Init"

expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: breaks'
expect 1 nix build "$flake1Dir#foo.bar" 2>&1 | grepQuiet 'error: breaks'

# Stack overflow error must not be cached
expect 1 nix build --max-call-depth 50 "$flake1Dir#stack-depth" 2>&1 \
  | grepQuiet 'error: stack overflow; max-call-depth exceeded'
# If the SO is cached, the following invocation will produce a cached failure; we expect it to succeed
nix build --no-link "$flake1Dir#stack-depth"

# Conditional error should not be cached
expect 1 nix build "$flake1Dir#ifd" --option allow-import-from-derivation false 2>&1 \
  | grepQuiet 'error: cannot build .* during evaluation because the option '\''allow-import-from-derivation'\'' is disabled'
nix build --no-link "$flake1Dir#ifd"
