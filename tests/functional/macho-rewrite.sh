#!/usr/bin/env bash

# Regression test for nixpkgs#507531 / NixOS/nix#6065.
#
# `RewritingSink` mutates bytes inside `__TEXT,__cstring` pages that
# Apple's `ld` had already covered with `linker-signed` SHA-256 page
# hashes in `LC_CODE_SIGNATURE`. Without the page-hash fix-up in
# `DerivationBuilderImpl::registerOutputs`, the rewritten binary
# SIGKILLs at first page-in with `cs_invalid_page`.
#
# IA trigger condition (CA is simpler; see the inline IA/CA split
# below): the bug only fires when at least one sibling output is
# already in the store at the start of a multi-output build, so that
# the sibling's scratch path becomes a `makeFallbackPath` placeholder
# which `RewritingSink` then substitutes for the final hash inside the
# already-signed `bin/hello`. A fresh cold build does not populate
# `outputRewrites` for siblings, so the bug does not trigger and a
# cold-build-only test would pass on an unpatched daemon. We therefore
# build cold and then re-invoke under `--check`, which is Nix's own
# determinism check: it rebuilds the derivation while the previous
# outputs are still in the store, materialising the corrupted binary in
# a `.check` directory. No `nix-store --delete` or other state mutation
# is required.
#
# `--check` is *expected* to fail with `may not be deterministic` here,
# because Apple's `ld_prime` generates a non-deterministic `LC_UUID`
# even with identical inputs (and macOS does not let us turn `LC_UUID`
# off — `dyld` rejects binaries that lack one). The test allows
# `--check` to fail and inspects the resulting `.check` binary.
#
# Assertions on the post-rewrite binary (`$out.check` in IA mode,
# `$out` in CA mode):
#   1. it executes (no SIGKILL — the load-bearing regression assertion:
#      any helper bug that produces stale page hashes manifests here as
#      a kernel kill)
#   2. its embedded `doc=` string contains the *final* doc hash, not the
#      pre-rewrite placeholder (proves the substitution actually
#      happened and the helper didn't accidentally restore pre-rewrite
#      bytes)
#   3. `codesign --verify` accepts the signature (catches any helper bug
#      that corrupts the SuperBlob structure)
#   4. `codesign -dvvv` reports `flags=0x20002(adhoc,linker-signed)`,
#      proving the original `ld` signature was preserved rather than
#      replaced by a `codesign(1)` re-sign (which would clear the
#      `linker-signed` bit and switch to 16 KiB pages)

source common.sh

if [[ $(uname) != Darwin ]]; then
    skipTest "Mach-O page-hash rewriting is darwin-specific"
fi

if ! [[ -x /usr/bin/cc ]]; then
    skipTest "Need /usr/bin/cc to build the test fixture"
fi

if ! [[ -x /usr/bin/codesign ]]; then
    skipTest "Need /usr/bin/codesign to validate signatures"
fi

# Build the multi-output derivation. The exact trigger condition
# differs between IA and CA derivations:
#
#   - **InputAddressed**: the bug only fires when at least one sibling
#     output is already in the store at the start of the build, so the
#     sibling's scratch path becomes a `makeFallbackPath` placeholder.
#     A fresh cold build does not populate `outputRewrites` for
#     siblings and therefore does not trigger the bug. We seed the
#     store with a cold build, then re-invoke under `nix-build --check`
#     — Nix's own determinism check rebuilds the derivation while the
#     previous outputs are still in the store, materialising the
#     corrupted (or fixed-up, on a patched daemon) binary in a `.check`
#     directory. No `nix-store --delete` or other state mutation is
#     required.
#
#   - **ContentAddressed** (issue NixOS/nix#6065 was filed against this
#     case): the cold build *itself* substitutes the scratch hash for
#     the final content-addressed hash inside the rewriteOutput lambda,
#     so the bug fires immediately. `--check` is unnecessary (and would
#     succeed silently rather than producing a `.check`, since CA
#     content-equality is verified per build).
#
# `NIX_TESTS_CA_BY_DEFAULT=1` is the framework convention used by the
# tests under `tests/functional/ca/`; it makes `mkDerivation` in the
# generated `config.nix` inject `__contentAddressed = true`,
# `outputHashMode = "recursive"`, `outputHashAlgo = "sha256"`.
out=$(nix-build --no-out-link ./macho-rewrite.nix)
[[ -x "$out/bin/hello" ]]

if [[ "${NIX_TESTS_CA_BY_DEFAULT:-}" == "1" ]]; then
    # CA: the cold build already triggered the bug. Run assertions
    # against `$out` directly.
    target="$out"
else
    # IA: trigger the bug via Nix's determinism check. Expected to
    # fail with `may not be deterministic` because Apple's `ld_prime`
    # generates a non-deterministic `LC_UUID` even with identical
    # inputs (and macOS does not let us turn `LC_UUID` off — `dyld`
    # rejects binaries that lack one), so the rebuild always differs
    # from the original. The `.check` directory contains the
    # post-rewrite-and-fixup binary. Exit code 104 is Nix's dedicated
    # status bit for `checkMismatch` — see `failingExitStatus` in
    # `src/libstore/build-result.cc`.
    expect 104 nix-build --no-out-link --check --keep-failed ./macho-rewrite.nix
    target="${out}.check"
    [[ -x "$target/bin/hello" ]]
fi

# Step 1 — binary executes (no SIGKILL — the load-bearing regression
# assertion: any helper bug that produces stale page hashes manifests
# here as a kernel kill)
output=$("$target/bin/hello")
echo "$output"

# Step 2 — embedded doc path is the final one (proves the substitution
# actually happened and the helper didn't accidentally restore
# pre-rewrite bytes). Match against `$NIX_STORE_DIR` rather than a
# hardcoded `/nix/store` so the assertion holds under the functional
# test harness, which points the store at a temporary directory.
echo "$output" | grep -qE "^doc=${NIX_STORE_DIR:-/nix/store}/[a-z0-9]{32}-macho-rewrite-multi-doc/share/doc/hello$"

# Step 3 — codesign verifies the (rewritten + fixed-up) signature
/usr/bin/codesign --verify "$target/bin/hello"

# Step 4 — `linker-signed` flag preserved (cleared by every
# `codesign(1)` invocation, so this catches any future refactor that
# swaps the in-process helper for a shellout)
/usr/bin/codesign -dvvv "$target/bin/hello" 2>&1 | grep -qF 'flags=0x20002(adhoc,linker-signed)'
