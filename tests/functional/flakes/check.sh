#!/usr/bin/env bash

source common.sh

flakeDir=$TEST_ROOT/flake3
mkdir -p "$flakeDir"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    overlay = final: prev: {
    };
  };
}
EOF

nix flake check "$flakeDir"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    overlay = finalll: prev: {
    };
  };
}
EOF

(! nix flake check "$flakeDir")

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self, ... }: {
    overlays.x86_64-linux.foo = final: prev: {
    };
  };
}
EOF

# shellcheck disable=SC2015
checkRes=$(nix flake check "$flakeDir" 2>&1 && fail "nix flake check --all-systems should have failed" || true)
echo "$checkRes" | grepQuiet "error: overlay is not a function, but a set instead"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    nixosModules.foo = {
      a.b.c = 123;
      foo = true;
    };
  };
}
EOF

nix flake check "$flakeDir"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    nixosModules.foo = assert false; {
      a.b.c = 123;
      foo = true;
    };
  };
}
EOF

(! nix flake check "$flakeDir")

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    nixosModule = { config, pkgs, ... }: {
      a.b.c = 123;
    };
  };
}
EOF

nix flake check "$flakeDir"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    packages.system-1.default = "foo";
    packages.system-2.default = "bar";
  };
}
EOF

nix flake check "$flakeDir"

# shellcheck disable=SC2015
checkRes=$(nix flake check --all-systems --keep-going "$flakeDir" 2>&1 && fail "nix flake check --all-systems should have failed" || true)
echo "$checkRes" | grepQuiet "packages.system-1.default"
echo "$checkRes" | grepQuiet "packages.system-2.default"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    apps.system-1.default = {
      type = "app";
      program = "foo";
    };
    apps.system-2.default = {
      type = "app";
      program = "bar";
      meta.description = "baz";
    };
  };
}
EOF

nix flake check --all-systems "$flakeDir"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    apps.system-1.default = {
      type = "app";
      program = "foo";
      unknown-attr = "bar";
    };
  };
}
EOF

# shellcheck disable=SC2015
checkRes=$(nix flake check --all-systems "$flakeDir" 2>&1 && fail "nix flake check --all-systems should have failed" || true)
echo "$checkRes" | grepQuiet "unknown-attr"

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    formatter.system-1 = "foo";
  };
}
EOF

# shellcheck disable=SC2015
checkRes=$(nix flake check --all-systems "$flakeDir" 2>&1 && fail "nix flake check --all-systems should have failed" || true)
echo "$checkRes" | grepQuiet "formatter.system-1"

# Test whether `nix flake check` builds checks.
cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    checks.$system.foo = with import ./config.nix; mkDerivation {
      name = "simple";
      buildCommand = "mkdir \$out";
    };
  };
}
EOF

cp "${config_nix}" "$flakeDir/"

expectStderr 0 nix flake check "$flakeDir" | grepQuiet 'running 1 flake check'

cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    checks.$system.foo = with import ./config.nix; mkDerivation {
      name = "simple";
      buildCommand = "false";
    };
  };
}
EOF

# FIXME: error code 100 doesn't get propagated from the daemon.
if ! isTestOnNixOS && $NIX_REMOTE != daemon; then
    expectStderr 100 nix flake check "$flakeDir" | grepQuiet 'builder failed with exit code 1'
fi

# Ensure non-substitutable (read: usually failed) checks are actually run
# https://github.com/NixOS/nix/pull/13574
cp "$config_nix" "$flakeDir"/
cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: with import ./config.nix; {
    checks.${system}.expectedToFail = derivation {
      name = "expected-to-fail";
      inherit system;
      builder = "not-a-real-file";
    };
  };
}
EOF

# NOTE: Regex pattern is used for compatibility with older daemon versions
# We also can't expect a specific status code. Earlier daemons return 1, but as of 2.31, we return 100
# shellcheck disable=SC2015
checkRes=$(nix flake check "$flakeDir" 2>&1 && fail "nix flake check should have failed" || true)
echo "$checkRes" | grepQuiet -E "builder( for .*)? failed with exit code 1"

# Test that attribute paths are shown in error messages
cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: with import ./config.nix; {
    checks.${system}.failingCheck = mkDerivation {
      name = "failing-check";
      buildCommand = "echo 'This check fails'; exit 1";
    };
    checks.${system}.anotherFailingCheck = mkDerivation {
      name = "another-failing-check";
      buildCommand = "echo 'This also fails'; exit 1";
    };
  };
}
EOF

# shellcheck disable=SC2015
checkRes=$(nix flake check --keep-going "$flakeDir" 2>&1 && fail "nix flake check should have failed" || true)
echo "$checkRes" | grepQuiet "checks.${system}.failingCheck"
echo "$checkRes" | grepQuiet "checks.${system}.anotherFailingCheck"
