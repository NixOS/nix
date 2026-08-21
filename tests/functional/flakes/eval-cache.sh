#!/usr/bin/env bash

source ./common.sh

requireGit

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

# Commands that auto-call the installable ('nix search', 'nix run', ...) must
# still use the cache: the trace fires while the cache is cold, and never again.
flake2Dir="$TEST_ROOT/eval-cache-auto-call-flake"

createGitRepo "$flake2Dir" ""
cp ../simple.nix ../simple.builder.sh "${config_nix}" "$flake2Dir/"
git -C "$flake2Dir" add simple.nix simple.builder.sh config.nix

cat >"$flake2Dir/flake.nix" <<EOF
{
  outputs = { self }: let inherit (import ./config.nix) mkDerivation; in {
    legacyPackages.$system = {};
    packages.$system = builtins.trace "evaluating packages" {
      "<auto-call>" = "real auto-call attribute";
      cached = mkDerivation {
        name = "cached";
        buildCommand = ''
          echo true > \$out
        '';
      };
    };
  };
}
EOF

git -C "$flake2Dir" add flake.nix
git -C "$flake2Dir" commit -m "Init"

# A real attribute with the old pseudo-entry name must occupy a distinct cache
# slot. Prime that real attribute first, then exercise the auto-call cache.
[[ $(nix eval --raw --no-write-lock-file "$flake2Dir#packages.$system.\"<auto-call>\"") == \
  "real auto-call attribute" ]]
nix search --no-write-lock-file "$flake2Dir" ^ 2>&1 | grepQuiet "evaluating packages"
nix search --no-write-lock-file "$flake2Dir" ^ 2>&1 | grepQuietInverse "evaluating packages"
