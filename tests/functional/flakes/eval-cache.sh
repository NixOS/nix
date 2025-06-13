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
    artificialTwoDifferentThrows = builtins.testThrowErrorMaybe (throw "real throw");
    artificialThrowOrDrv = builtins.testThrowErrorMaybe self.drv;
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

# make sure this builtin does not reach production
nix eval --expr 'assert ! (builtins ? testThrowErrorMaybe); null'

# and that it does reach the testing environment
export _NIX_TEST_PRIMOPS=1
nix eval --expr 'assert (builtins.testThrowErrorMaybe true); null'

# and that it works
_NIX_TEST_DO_THROW_ERROR=1 expect 1 nix eval --expr 'builtins.testThrowErrorMaybe true' 2>&1 \
  | grepQuiet 'this is a dummy error'


# make sure error details do not get cached
_NIX_TEST_DO_THROW_ERROR=1 expect 1 nix eval "$flake1Dir#artificialTwoDifferentThrows" 2>&1 \
  | grepQuiet "this is a dummy error"
expect 1 nix eval "$flake1Dir#artificialTwoDifferentThrows" 2>&1 \
  | grepQuiet "real throw"

# If a cached error cannot be reproduced, do not continue as usual.
# Instead, throw an error.
#
# To be clear, the following test case should never occur in production.
# But it might, due to an implementation error in Nix or plugins.

# create an artificial exception cache entry
_NIX_TEST_DO_THROW_ERROR=1 expect 1 nix build "$flake1Dir#artificialThrowOrDrv" 2>&1 \
  | grepQuiet "this is a dummy error"

# Invoke again but without the artificial throwing of an exception
expect 1 nix build "$flake1Dir#artificialThrowOrDrv" 2>&1 \
  | grepQuiet "unexpected evaluation success despite the existence of a cached error. This should not happen. It is a bug in the implementation of Nix or a plugin. A transient exception should not have been cached."
