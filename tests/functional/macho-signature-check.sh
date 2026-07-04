#!/usr/bin/env bash

# Tests for the `macho-signature-rewrite-check` setting.
#
# `RewritingSink` substitutes scratch-path hashes with final hashes
# inside build outputs after the builder has exited. When the
# substituted bytes sit inside a Mach-O file carrying
# `LC_CODE_SIGNATURE`, the signature's page hashes no longer match
# and the macOS kernel kills the binary at first page-in
# (nixpkgs#507531, NixOS/nix#6065). The check turns that silent
# corruption into a build error.
#
# The rewrite fires when an output being built is already present in
# the store at build start (its scratch path becomes a synthesised
# fallback path). We drive it two ways: deleting one output of a
# multi-output derivation and rebuilding (the partial-substitution
# shape), and `--check` (all outputs present).

source common.sh

TODO_NixOS # Requires clearing the store

[[ $(uname) == Darwin ]] || skipTest "Mach-O binaries can only be built on darwin"
[[ -x /usr/bin/cc ]] || skipTest "Need /usr/bin/cc to build the test fixture"
[[ -x /usr/bin/python3 ]] || skipTest "Need /usr/bin/python3 to synthesize the CMS fixture"

clearStore

drv=$(nix-instantiate ./macho-signature-check.nix)

# Cold build: no outputs present, no rewrite, no error.
nix-store --realise "$drv"
out=$(nix-store --query --outputs "$drv" | grep -v -- '-doc$')
[[ -x "$out/bin/hello" ]]

if [[ -n "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    skipTest "the fixture's rewrite trigger below is the input-addressed partial-rebuild shape"
fi

# Delete `out`, keep `doc`, rebuild. The doc output's presence forces
# hash rewriting in the freshly built `out`, whose signed binary
# embeds the doc path → refused by default.
nix-store --delete "$out"
expectStderr 1 nix-store --realise "$drv" >"$TEST_ROOT/refuse.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/refuse.stderr"

# The error names the already-present output whose deletion allows a
# clean rebuild...
docPath=$(nix-store --query --outputs "$drv" | grep -- '-doc$')
grepQuiet "$docPath" "$TEST_ROOT/refuse.stderr"

# ...and the signed binary, but NOT its unsigned twin, which embeds
# the same placeholder and undergoes the same rewrite harmlessly.
grepQuiet 'bin/hello"' "$TEST_ROOT/refuse.stderr"
grepQuietInverse "hello-unsigned" "$TEST_ROOT/refuse.stderr"

# warn: build succeeds (registering the broken binary), diagnostic on
# stderr; both binaries are registered.
nix-store --realise "$drv" --option macho-signature-rewrite-check warn 2>&1 |
    grepQuiet "invalidates its macOS code signature"
[[ -x "$out/bin/hello" ]]
[[ -x "$out/bin/hello-unsigned" ]]

# With all outputs present and valid, the guard also fires under
# --check: every output's scratch path is a fallback, so the rewrite
# would invalidate the signature the same way. Previously this
# surfaced as a spurious "may not be deterministic" failure.
expectStderr 1 nix-store --realise "$drv" --check |
    grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file"

# ignore: previous behaviour, silent.
nix-store --delete "$out"
nix-store --realise "$drv" --option macho-signature-rewrite-check ignore 2>&1 |
    grepQuietInverse "code signature"
[[ -x "$out/bin/hello" ]]

# CMS-signed (Developer-ID-shaped) binary: refused with a distinct
# annotation and explanation, since no re-signing without the
# original identity can ever produce a valid signature.
cmsDrv=$(nix-instantiate ./macho-signature-cms.nix)
nix-store --realise "$cmsDrv"
cmsOut=$(nix-store --query --outputs "$cmsDrv" | grep -v -- '-doc$')
[[ -x "$cmsOut/bin/hello-cms" ]]
nix-store --delete "$cmsOut"
expectStderr 1 nix-store --realise "$cmsDrv" >"$TEST_ROOT/cms-refuse.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/cms-refuse.stderr"
grepQuiet "(CMS-signed)" "$TEST_ROOT/cms-refuse.stderr"
grepQuiet "cannot be re-signed without the original signing identity" "$TEST_ROOT/cms-refuse.stderr"
