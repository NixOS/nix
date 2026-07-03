#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
# A/B build of Nix: identical GCC release build, interposition flags OFF vs ON.
# Run inside `nix develop` (GCC 15.2.0 stdenv).
set -euo pipefail
SCR=${SCRATCH}
REPO=/home/siraben/nix
cd "$REPO"

echo "== compiler =="; $CXX --version | head -1

# Common meson options: release optimization, no docs/tests to speed build.
COMMON=(--buildtype=release -Ddoc-gen=false -Dunit-tests=false -Dfunctional-tests=false -Djson-schema-checks=false)

build_variant () {
  local name="$1"; shift
  local dir="$SCR/build-$name"
  rm -rf "$dir"
  echo "== configuring $name =="
  env "$@" meson setup "$dir" "$REPO" "${COMMON[@]}" 2>&1 | tail -3
  echo "== building $name =="
  ninja -C "$dir" -j"$(nproc)" src/nix/nix 2>&1 | tail -3
  ls -la "$dir/src/nix/nix"
}

# Baseline: no interposition flags.
build_variant base

# Interposition: -fno-semantic-interposition + -Wl,-Bsymbolic-functions (GCC-only win).
build_variant interp \
  CFLAGS="-fno-semantic-interposition" \
  CXXFLAGS="-fno-semantic-interposition" \
  LDFLAGS="-Wl,-Bsymbolic-functions"

echo "ALL_BUILDS_DONE"
