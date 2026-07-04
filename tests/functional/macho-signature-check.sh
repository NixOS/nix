#!/usr/bin/env bash

# Tests for the `macho-signature-rewrite-check` setting and the
# `macho-signature-repair-hook` repair.
#
# `RewritingSink` substitutes scratch-path hashes with final hashes
# inside build outputs after the builder has exited. When the
# substituted bytes sit inside a Mach-O file carrying
# `LC_CODE_SIGNATURE`, the signature's page hashes no longer match
# and the macOS kernel kills the binary at first page-in
# (nixpkgs#507531, NixOS/nix#6065). The check detects this; by
# default the signature repair hook then repairs the stale page hashes
# in place, and with the hook disabled the build fails instead.
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
[[ -x /usr/bin/codesign ]] || skipTest "Need /usr/bin/codesign to validate signatures"
[[ -x /usr/bin/python3 ]] || skipTest "Need /usr/bin/python3 to synthesize the CMS fixture"

clearStore

drv=$(nix-instantiate ./macho-signature-check.nix)

# Cold build: no outputs present, no rewrite, nothing to detect.
nix-store --realise "$drv"
out=$(nix-store --query --outputs "$drv" | grep -v -- '-doc$')
[[ -x "$out/bin/hello" ]]
docPath=$(nix-store --query --outputs "$drv" | grep -- '-doc$')

if [[ -n "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    skipTest "the fixture's rewrite trigger below is the input-addressed partial-rebuild shape"
fi

# Delete `out`, keep `doc`, rebuild. The doc output's presence forces
# hash rewriting in the freshly built `out`, invalidating the signed
# binary's page hashes. The default signature repair hook repairs them:
# the build succeeds and the binary carries a valid signature.
nix-store --delete "$out"
nix-store --realise "$drv"
[[ -x "$out/bin/hello" ]]
/usr/bin/codesign --verify "$out/bin/hello"
"$out/bin/hello" | grepQuiet "^doc=$docPath"

# Same rebuild with the hook disabled: detect-and-refuse.
nix-store --delete "$out"
expectStderr 1 nix-store --realise "$drv" --option macho-signature-repair-hook "" >"$TEST_ROOT/refuse.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/refuse.stderr"

# The error names the already-present output whose deletion allows a
# clean rebuild...
grepQuiet "$docPath" "$TEST_ROOT/refuse.stderr"

# ...and the signed binary, but NOT its unsigned twin, which embeds
# the same placeholder and undergoes the same rewrite harmlessly.
grepQuiet 'bin/hello"' "$TEST_ROOT/refuse.stderr"
grepQuietInverse "hello-unsigned" "$TEST_ROOT/refuse.stderr"

# A failing hook fails closed to the same refusal.
expectStderr 1 nix-store --realise "$drv" --option macho-signature-repair-hook "$coreutils/false" >"$TEST_ROOT/hookfail.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/hookfail.stderr"
grepQuiet "The signature repair hook failed to repair" "$TEST_ROOT/hookfail.stderr"

# A custom hook must implement the --check contract: after any repair
# the hook is re-invoked with --check, and only a clean result lets
# the build proceed. A hook that repairs correctly but errors on
# --check is a refusal, not a registration on the repair's word.
cat > "$TEST_ROOT/no-check-hook.sh" <<EOF
#!/bin/sh
if [ "\$1" = --check ]; then exit 1; fi
exec $(type -p nix) __fixup-macho "\$@"
EOF
chmod +x "$TEST_ROOT/no-check-hook.sh"
expectStderr 1 nix-store --realise "$drv" --option macho-signature-repair-hook "$TEST_ROOT/no-check-hook.sh" >"$TEST_ROOT/nocheck.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/nocheck.stderr"
grepQuiet "Re-checking the repaired file(s) failed" "$TEST_ROOT/nocheck.stderr"

# warn: build succeeds registering the broken binary, diagnostic on
# stderr; no repair is attempted.
nix-store --realise "$drv" --option macho-signature-rewrite-check warn 2>&1 |
    grepQuiet "invalidates its macOS code signature"
[[ -x "$out/bin/hello" ]]
[[ -x "$out/bin/hello-unsigned" ]]

# With all outputs present and valid, the rewrite also fires under
# --check. With the hook disabled that is a refusal (previously a
# spurious "may not be deterministic" failure)...
expectStderr 1 nix-store --realise "$drv" --check --option macho-signature-repair-hook "" |
    grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file"

# ...and with the default hook the repair succeeds, leaving only the
# genuine LC_UUID link nondeterminism for the determinism comparison
# to report.
expect 104 nix-store --realise "$drv" --check

# ignore: previous behaviour, silent.
nix-store --delete "$out"
nix-store --realise "$drv" --option macho-signature-rewrite-check ignore 2>&1 |
    grepQuietInverse "code signature"
[[ -x "$out/bin/hello" ]]

# Direct repair-tool exercise, dual-oracle: corrupt one byte of a
# string the signature covers, confirm codesign rejects it, repair,
# confirm codesign accepts it and the byte survived (the repair
# touches only hash slots, never content).
cp "$out/bin/hello" "$TEST_ROOT/corrupted"
chmod +w "$TEST_ROOT/corrupted"
/usr/bin/python3 - "$TEST_ROOT/corrupted" <<'EOF'
import sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
off = data.find(b"doc=")
assert off > 0
data[off] = ord("D")
open(path, "wb").write(data)
EOF
expect 1 /usr/bin/codesign --verify "$TEST_ROOT/corrupted"

# --check contract: exit 2 = stale hashes present, repairs nothing.
expect 2 nix __fixup-macho --check "$TEST_ROOT/corrupted"
expect 1 /usr/bin/codesign --verify "$TEST_ROOT/corrupted"

nix __fixup-macho "$TEST_ROOT/corrupted" 2>&1 | grepQuiet "rewrote 1 file(s)"
/usr/bin/codesign --verify "$TEST_ROOT/corrupted"
"$TEST_ROOT/corrupted" | grepQuiet "^Doc="

# --check contract: exit 0 = all signatures valid.
nix __fixup-macho --check "$TEST_ROOT/corrupted"

# CMS-signed (Developer-ID-shaped) binary: never repaired. The
# rewrite guard refuses even with the default repair hook enabled,
# annotating the file, and the error explains why.
cmsDrv=$(nix-instantiate ./macho-signature-cms.nix)
nix-store --realise "$cmsDrv"
cmsOut=$(nix-store --query --outputs "$cmsDrv" | grep -v -- '-doc$')
[[ -x "$cmsOut/bin/hello-cms" ]]
nix-store --delete "$cmsOut"
expectStderr 1 nix-store --realise "$cmsDrv" >"$TEST_ROOT/cms-refuse.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/cms-refuse.stderr"
grepQuiet "(CMS-signed)" "$TEST_ROOT/cms-refuse.stderr"
grepQuiet "cannot be re-signed without the original signing identity" "$TEST_ROOT/cms-refuse.stderr"

# A signature the repair tool cannot process (unsupported hash type):
# detection classifies the file repairable, the hook runs and exits 0
# having skipped it, and only the re-check catches that the signature
# was never verified. The build must be refused, not registered —
# trusting the hook's exit status here is how a still-broken binary
# would slip into the store under the default `refuse`.
unsupDrv=$(nix-instantiate ./macho-signature-unsupported.nix)
nix-store --realise "$unsupDrv"
unsupOut=$(nix-store --query --outputs "$unsupDrv" | grep -v -- '-doc$')
nix-store --delete "$unsupOut"
expectStderr 1 nix-store --realise "$unsupDrv" >"$TEST_ROOT/unsupported.stderr"
grepQuiet "refusing to rewrite store path hashes inside signed Mach-O file" "$TEST_ROOT/unsupported.stderr"
grepQuiet "exited successfully but left signatures" "$TEST_ROOT/unsupported.stderr"

# Register the broken CMS binary anyway (warn mode), then confirm the
# at-rest sweep skips it with a warning instead of failing the batch.
nix-store --realise "$cmsDrv" --option macho-signature-rewrite-check warn 2>&1 |
    grepQuiet "invalidates its macOS code signature"
nix store fixup-macho "$cmsOut" 2>&1 >"$TEST_ROOT/cms-sweep.out" | tee "$TEST_ROOT/cms-sweep.stderr" >/dev/null
grepQuiet "not repairing" "$TEST_ROOT/cms-sweep.stderr"
grepQuiet "CMS signature" "$TEST_ROOT/cms-sweep.stderr"

# Batch resilience: an unrepairable path earlier in the batch must not
# prevent a repairable path later in the same invocation. Build a
# genuinely broken (ad-hoc) path alongside the CMS one and sweep both
# in one call (CMS listed first). The CMS path is skipped with a
# warning; the broken path is still repaired and verifies.
brokenDrv=$(nix-instantiate ./macho-signature-check.nix)
nix-store --realise "$brokenDrv" >/dev/null
brokenOut=$(nix-store --query --outputs "$brokenDrv" | grep -v -- '-doc$')
nix-store --delete "$brokenOut"
nix-store --realise "$brokenDrv" --option macho-signature-rewrite-check warn --option macho-signature-repair-hook "" 2>&1 |
    grepQuiet "invalidates its macOS code signature"
expect 1 /usr/bin/codesign --verify "$brokenOut/bin/hello"
# nix logging (printInfo/warn) goes to stderr; capture both streams.
nix store fixup-macho "$cmsOut" "$brokenOut" >"$TEST_ROOT/batch.log" 2>&1
grepQuiet "not repairing" "$TEST_ROOT/batch.log" # cms skipped
grepQuiet "repaired '" "$TEST_ROOT/batch.log"    # broken repaired despite cms earlier
/usr/bin/codesign --verify "$brokenOut/bin/hello"
