#!/usr/bin/env bash

# The content-addressed trigger of `macho-signature-rewrite-check`:
# a CA output's final hash is only known after the build, so a signed
# binary embedding its own output path must be rewritten on every
# cold build — no store state needed. The guard refuses, with the
# CA-specific remediation (no rebuild can preserve the signature):
# the hashed pages contain the output's own path, which is a function
# of those pages, so no consistent repaired value exists
# (NixOS/nix#6065).

source common.sh

[[ $(uname) == Darwin ]] || skipTest "Mach-O binaries can only be built on darwin"
[[ -x /usr/bin/cc ]] || skipTest "Need /usr/bin/cc to build the test fixture"

clearStore

drv=$(nix-instantiate ./macho-signature-check.nix)

# Cold CA build: refused, with the self-reference remediation.
expectStderr 1 nix-store --realise "$drv" >"$TEST_ROOT/ca-refuse.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/ca-refuse.stderr"
grepQuiet "content-addressed" "$TEST_ROOT/ca-refuse.stderr"
grepQuiet "no rebuild can currently preserve the signature" "$TEST_ROOT/ca-refuse.stderr"

# warn: registers the (broken) output.
nix-store --realise "$drv" --option macho-signature-rewrite-check warn 2>&1 |
    grepQuiet "invalidates its macOS code signature"
