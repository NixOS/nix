#!/usr/bin/env bash

source ../common.sh

TODO_NixOS

clearStore
rm -rf "$TEST_HOME"/.cache "$TEST_HOME"/.config "$TEST_HOME"/.local

cp ../shell-hello.nix "${config_nix}" "$TEST_HOME"
cd "$TEST_HOME"

# Helper: set up a test directory with config.nix accessible
setupTestDir() {
    local dir="$1"
    mkdir -p "$dir"
    cp "${config_nix}" "$dir/config.nix"
    cd "$dir"
}

# flake.nix takes priority over default.nix
setupTestDir "$TEST_HOME/flake-priority"
cp ../shell-hello.nix .

cat <<EOF > flake.nix
{
  outputs = _: {
    packages.$system.default = (import ./shell-hello.nix).hello;
  };
}
EOF

cat <<EOF > default.nix
with import ./config.nix;
mkDerivation {
  name = "test-run";
  buildCommand = ''
    mkdir -p \$out/bin
    cat > \$out/bin/test-run <<INNER
    #! ${SHELL}
    echo "default"
    INNER
    chmod +x \$out/bin/test-run
  '';
}
EOF

# When both exist, flake.nix wins
nix run --no-write-lock-file . 2>&1 | grepQuiet "Hello"

# default.nix fallback when no flake.nix exists
setupTestDir "$TEST_HOME/default-only"

cat <<EOF > default.nix
with import ./config.nix;
mkDerivation {
  name = "test-run";
  buildCommand = ''
    mkdir -p \$out/bin
    cat > \$out/bin/test-run <<INNER
    #! ${SHELL}
    echo "hello from default.nix"
    INNER
    chmod +x \$out/bin/test-run
  '';
}
EOF

nix run --no-write-lock-file . 2>&1 | grepQuiet "hello from default.nix"

# default.nix as a function
setupTestDir "$TEST_HOME/default-system"

cat <<EOF > default.nix
{ system ? builtins.currentSystem }:
with import ./config.nix;
mkDerivation {
  name = "test-run";
  buildCommand = ''
    mkdir -p \$out/bin
    cat > \$out/bin/test-run <<INNER
    #! ${SHELL}
    echo "system=\${system}"
    INNER
    chmod +x \$out/bin/test-run
  '';
}
EOF

nix run --no-write-lock-file . 2>&1 | grepQuiet "system="

# Explicit attribute
setupTestDir "$TEST_HOME/default-fragment"

cat <<EOF > default.nix
with import ./config.nix;
{
  hello = mkDerivation {
    name = "hello";
    buildCommand = ''
      mkdir -p \$out/bin
      cat > \$out/bin/hello <<INNER
      #! ${SHELL}
      echo "fragment-hello"
      INNER
      chmod +x \$out/bin/hello
    '';
  };
  world = mkDerivation {
    name = "world";
    buildCommand = ''
      mkdir -p \$out/bin
      cat > \$out/bin/world <<INNER
      #! ${SHELL}
      echo "fragment-world"
      INNER
      chmod +x \$out/bin/world
    '';
  };
}
EOF

nix run --no-write-lock-file .#hello 2>&1 | grepQuiet "fragment-hello"
nix run --no-write-lock-file .#world 2>&1 | grepQuiet "fragment-world"

# Top-level derivation is used directly when no fragment given
setupTestDir "$TEST_HOME/default-toplevel-drv"

cat <<EOF > default.nix
with import ./config.nix;
mkDerivation {
  name = "test-run";
  buildCommand = ''
    mkdir -p \$out/bin
    cat > \$out/bin/test-run <<INNER
    #! ${SHELL}
    echo "toplevel-derivation"
    INNER
    chmod +x \$out/bin/test-run
  '';
}
EOF

nix run --no-write-lock-file . 2>&1 | grepQuiet "toplevel-derivation"

# Neither flake.nix nor default.nix: should error
mkdir -p "$TEST_HOME/empty-dir"
cd "$TEST_HOME/empty-dir"
expectStderr 1 nix run --no-write-lock-file . | grepQuiet "does not contain"

# path:// & absolute path
cd "$TEST_HOME/default-only"
nix run --no-write-lock-file "path://$TEST_HOME/default-only" 2>&1 | grepQuiet "hello from default.nix"
nix run --no-write-lock-file "$TEST_HOME/default-only" 2>&1 | grepQuiet "hello from default.nix"

## Extended test cases

setupTestDir "$TEST_HOME/pure-eval"

# Use pure-eval by default
cat <<'EOF' > default.nix
{ value = builtins.currentSystem; }
EOF

# builtins.currentSystem is impure-only, so this must fail in pure eval mode
expectStderr 1 nix eval --no-write-lock-file .#value | grepQuiet "currentSystem"


# Local default.nix should NOT be copied to the store
setupTestDir "$TEST_HOME/no-store-copy"

echo "unique-marker-$$" > only-local.txt

cat <<'EOF' > default.nix
{ value = "no-copy"; }
EOF

nix eval --no-write-lock-file .#value --raw > /dev/null

# If the directory was copied to the store, only-local.txt would appear there
[[ -z "$(find "$NIX_STORE_DIR" -name only-local.txt 2>/dev/null)" ]] \
    || fail "default.nix source tree was copied to the store"

# Explicit attribute wins over top-level derivation
setupTestDir "$TEST_HOME/fragment-over-drv"

cat <<EOF > default.nix
with import ./config.nix;
let
  main = mkDerivation {
    name = "test-run";
    buildCommand = ''
      mkdir -p \$out/bin
      cat > \$out/bin/test-run <<INNER
      #! ${SHELL}
      echo "toplevel"
      INNER
      chmod +x \$out/bin/test-run
    '';
  };
  sub = mkDerivation {
    name = "sub";
    buildCommand = ''
      mkdir -p \$out/bin
      cat > \$out/bin/sub <<INNER
      #! ${SHELL}
      echo "sub-attr"
      INNER
      chmod +x \$out/bin/sub
    '';
  };
in main // { inherit sub; }
EOF

# No fragment: top-level derivation is used
nix run --no-write-lock-file . 2>&1 | grepQuiet "toplevel"
# Fragment given: sub-attribute wins over top-level derivation
nix run --no-write-lock-file .#sub 2>&1 | grepQuiet "sub-attr"

# Should NOT search upward for default.nix
setupTestDir "$TEST_HOME/no-search-up"

cat <<'EOF' > default.nix
{ value = "parent"; }
EOF

mkdir -p child
cd child

# child/ has no default.nix; parent has one. Should fail, not find parent's.
expectStderr 1 nix eval --no-write-lock-file .#value | grepQuiet "does not contain"

# nix build works with default.nix
setupTestDir "$TEST_HOME/build-test"

cat <<EOF > default.nix
with import ./config.nix;
mkDerivation {
  name = "test-run";
  buildCommand = ''
    mkdir -p \$out/bin
    cat > \$out/bin/test-run <<INNER
    #! ${SHELL}
    echo "built from default.nix"
    INNER
    chmod +x \$out/bin/test-run
  '';
}
EOF

nix build --no-write-lock-file --no-link .
# Also verify the built result is runnable
nix run --no-write-lock-file . 2>&1 | grepQuiet "built from default.nix"

# --impure restores builtins.currentSystem access
setupTestDir "$TEST_HOME/impure-override"

cat <<'EOF' > default.nix
{ value = builtins.currentSystem; }
EOF

# Pure eval (default) must fail
expectStderr 1 nix eval --no-write-lock-file .#value | grepQuiet "currentSystem"
# --impure must succeed
nix eval --no-write-lock-file --impure .#value

# Lambda that doesn't accept system should fail with a clear error
setupTestDir "$TEST_HOME/bad-lambda"

cat <<'EOF' > default.nix
{ pkgs }: pkgs
EOF

# The lambda has no 'system' param, so auto-pass doesn't trigger.
# nix build needs a derivation, so this should error.
expectStderr 1 nix build --no-write-lock-file --no-link .

# Flakes explicitly disabled
# default.nix used, flake.nix ignored
setupTestDir "$TEST_HOME/no-flakes"

cat <<EOF > flake.nix
{ inputs = throw "did you mean to access a flake.nix?"; }
EOF

cat <<EOF > default.nix
with import ./config.nix;
mkDerivation {
  name = "test-run";
  buildCommand = ''
    mkdir -p \$out/bin
    cat > \$out/bin/test-run <<INNER
    #! ${SHELL}
    echo "no-flakes"
    INNER
    chmod +x \$out/bin/test-run
  '';
}
EOF

nix run --option experimental-features "nix-command" --no-write-lock-file . 2>&1 | grepQuiet "no-flakes"

# Nested attribute paths: .#a.b
setupTestDir "$TEST_HOME/nested-fragment"

cat <<EOF > default.nix
with import ./config.nix;
{
  a.b = mkDerivation {
    name = "nested";
    buildCommand = ''
      mkdir -p \$out/bin
      cat > \$out/bin/nested <<INNER
      #! ${SHELL}
      echo "nested-attr"
      INNER
      chmod +x \$out/bin/nested
    '';
  };
}
EOF

nix run --no-write-lock-file '.#a.b' 2>&1 | grepQuiet "nested-attr"
