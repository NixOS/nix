#!/usr/bin/env bash

# Tests for `macho-signature-verify` (the substitution-time check)
# and `nix store fixup-macho` (the at-rest repair).
#
# A binary whose signature page hashes are stale can enter a store
# via substitution — broken where it was built, by whatever broke it
# (the producing daemon, a build tool, an upstream artifact). The
# substitution check catches it at the door; the at-rest command
# repairs what is already inside.

source common.sh

TODO_NixOS # Requires clearing the store

[[ $(uname) == Darwin ]] || skipTest "Mach-O binaries can only be built on darwin"
[[ -x /usr/bin/cc ]] || skipTest "Need /usr/bin/cc to build the test fixture"
[[ -x /usr/bin/codesign ]] || skipTest "Need /usr/bin/codesign to validate signatures"
[[ -x /usr/bin/python3 ]] || skipTest "Need /usr/bin/python3 to synthesize the unsupported-hash fixture"

if [[ -n "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    skipTest "the broken-binary fixture relies on the input-addressed rewrite shape"
fi

clearStore

cacheDir="$TEST_ROOT/binary-cache"

drv=$(nix-instantiate ./macho-signature-check.nix)

# Manufacture a BROKEN cached artifact: build cold, delete `out`,
# rebuild with the rewrite under `warn` and repair disabled — the
# registered binary then carries stale page hashes, exactly like the
# real-world broken cache entries. Then publish it.
nix-store --realise "$drv"
out=$(nix-store --query --outputs "$drv" | grep -v -- '-doc$')
docPath=$(nix-store --query --outputs "$drv" | grep -- '-doc$')
nix-store --delete "$out"
nix-store --realise "$drv" \
    --option macho-signature-rewrite-check warn \
    --option macho-signature-repair-hook "" 2>&1 |
    grepQuiet "invalidates its macOS code signature"
expect 1 /usr/bin/codesign --verify "$out/bin/hello"

nix copy --to "file://$cacheDir" "$out" --no-check-sigs

# Substitute it back under each mode. `ignore` (default): broken
# binary comes through silently, as before.
clearStore
nix-store --realise "$docPath" --option substituters "file://$cacheDir" --no-require-sigs
nix-store --realise "$out" --option substituters "file://$cacheDir" --no-require-sigs 2>&1 |
    grepQuietInverse "code signature"
expect 1 /usr/bin/codesign --verify "$out/bin/hello"

# warn: named diagnosis at download time.
clearStore
nix-store --realise "$out" --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify warn 2>&1 |
    grepQuiet "invalid code signatures"
[[ -x "$out/bin/hello" ]]

# refuse: the substitution fails. Realising the bare path cannot fall
# back to a build and fails outright...
clearStore
expectStderr 1 nix-store --realise "$out" --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify refuse |
    grepQuiet "refusing to add"
# ...but realising the derivation with --fallback builds locally
# instead (cold build, no rewrite → valid signature).
drv=$(nix-instantiate ./macho-signature-check.nix) # clearStore deleted it
nix-store --realise "$drv" --fallback --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify refuse 2>"$TEST_ROOT/refuse.stderr"
/usr/bin/codesign --verify "$out/bin/hello"

# repair: the path is fixed before registration and registered
# unsigned with an updated NAR hash.
clearStore
nix-store --realise "$out" --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify repair 2>&1 |
    grepQuiet "repaired invalid Mach-O code signature"
/usr/bin/codesign --verify "$out/bin/hello"
"$out/bin/hello" | grepQuiet "^doc=$docPath"
# The recorded NAR hash matches the repaired contents.
nix store verify --no-trust "$out"
# The substituter's signatures signed the old contents; the repaired
# path must be registered unsigned.
nix path-info --json --json-format 2 "$out" | jq -e '.info.[].signatures // [] | length == 0' >/dev/null

# At-rest sweep: substitute the broken path (ignore mode), then
# repair it in place in the store.
clearStore
nix-store --realise "$out" --option substituters "file://$cacheDir" --no-require-sigs
expect 1 /usr/bin/codesign --verify "$out/bin/hello"

nix store fixup-macho --dry-run "$out" 2>&1 | grepQuiet "would repair"
expect 1 /usr/bin/codesign --verify "$out/bin/hello"

nix store fixup-macho "$out" 2>&1 | grepQuiet "repaired"
/usr/bin/codesign --verify "$out/bin/hello"
"$out/bin/hello" | grepQuiet "^doc=$docPath"
nix store verify --no-trust "$out"

# Idempotent: a second sweep finds nothing.
nix store fixup-macho --dry-run "$out" 2>&1 | grepQuiet "0 path(s)"

# A signature the repair tool cannot process (unsupported hash type):
# `repair` runs the hook, which skips the file and exits 0. The
# re-check catches that nothing was verified, so the path is added
# with a warning — not reported repaired, and the path info is only
# rewritten to match bytes the repair actually changed (here: none).
clearStore
unsupDrv=$(nix-instantiate ./macho-signature-unsupported.nix)
nix-store --realise "$unsupDrv"
unsupOut=$(nix-store --query --outputs "$unsupDrv" | grep -v -- '-doc$')
nix copy --to "file://$cacheDir" "$unsupOut" --no-check-sigs

clearStore
nix-store --realise "$unsupOut" --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify repair 2>&1 |
    tee "$TEST_ROOT/unsup-repair.stderr" >/dev/null
grepQuiet "did not repair" "$TEST_ROOT/unsup-repair.stderr"
grepQuietInverse "repaired invalid Mach-O code signature" "$TEST_ROOT/unsup-repair.stderr"
# The path was added unmodified, so the substituted NAR still verifies.
nix store verify --no-trust "$unsupOut"

# ...and under `refuse` an unverifiable signature is refused, the
# same as a stale one.
clearStore
expectStderr 1 nix-store --realise "$unsupOut" \
    --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify refuse |
    grepQuiet "refusing to add"

# The at-rest sweep on the same path: the repair tool runs but cannot
# process the signature, and the re-check on the repaired copy fails —
# the copy must not be swapped in, and the path stays intact.
clearStore
nix-store --realise "$unsupOut" --option substituters "file://$cacheDir" --no-require-sigs
nix store fixup-macho "$unsupOut" 2>&1 | grepQuiet "still invalid after repair"
nix store verify --no-trust "$unsupOut"

# Partial repair: a binary carrying a normal SHA-256 CodeDirectory
# plus an unsupported-hash alternate. The repair fixes the supported
# one — changing bytes — but the re-check still fails on the alternate,
# so the path is registered warned-not-repaired with its recorded NAR
# hash matching the partially-repaired bytes actually on disk.
clearStore
partialDrv=$(nix-instantiate ./macho-signature-partial.nix)
nix-store --realise "$partialDrv"
partialOut=$(nix-store --query --outputs "$partialDrv" | grep -v -- '-doc$')
nix-store --delete "$partialOut"
nix-store --realise "$partialDrv" \
    --option macho-signature-rewrite-check warn \
    --option macho-signature-repair-hook "" 2>&1 |
    grepQuiet "invalidates its macOS code signature"
nix copy --to "file://$cacheDir" "$partialOut" --no-check-sigs

clearStore
nix-store --realise "$partialOut" --option substituters "file://$cacheDir" --no-require-sigs \
    --option macho-signature-verify repair 2>&1 |
    tee "$TEST_ROOT/partial-repair.stderr" >/dev/null
grepQuiet "did not repair" "$TEST_ROOT/partial-repair.stderr"
grepQuietInverse "repaired invalid Mach-O code signature" "$TEST_ROOT/partial-repair.stderr"
# The repair changed bytes, so the recorded NAR hash must describe the
# on-disk state (updated), and the substituter's signatures must be
# gone — the info describes bytes the substituter never signed.
nix store verify --no-trust "$partialOut"
nix path-info --json --json-format 2 "$partialOut" | jq -e '.info.[].signatures // [] | length == 0' >/dev/null

# A Mach-O file too large to parse cannot be verified: the check
# child skips it, so its exit status says nothing about the file.
# Under `refuse` the path must be refused as unverifiable — trusting
# the child's exit 0 here would accept a possibly-broken binary the
# check never looked at. (Sparse file: only the magic is real.)
clearStore
bigDrv=$(nix-instantiate ./macho-signature-oversized.nix)
nix-store --realise "$bigDrv"
bigOut=$(nix-store --query --outputs "$bigDrv")
nix copy --to "file://$cacheDir?compression=none" "$bigOut" --no-check-sigs

clearStore
expectStderr 1 nix-store --realise "$bigOut" \
    --option substituters "file://$cacheDir?compression=none" --no-require-sigs \
    --option macho-signature-verify refuse |
    grepQuiet "too large to have their code signatures verified"

# warn: accepted, with the unverifiable file called out.
nix-store --realise "$bigOut" \
    --option substituters "file://$cacheDir?compression=none" --no-require-sigs \
    --option macho-signature-verify warn 2>&1 |
    grepQuiet "too large to have their code signatures verified"
[[ -e "$bigOut/big" ]]

# The tool's own --check contract agrees: an uninspectable Mach-O is
# a check failure, not a pass.
expect 2 nix __fixup-macho --check "$bigOut"
