#!/usr/bin/env bash

# Regression test for nixpkgs#507531 / NixOS/nix#6065.
#
# `RewritingSink` mutates bytes inside pages that `ld` had already
# covered with SHA-256 page hashes. Without the fix-up, the binary
# SIGKILLs at first page-in.
#
# In IA mode the bug fires when an output the binary references is
# already in the store at build start (self-reference or sibling);
# its scratch path becomes a `makeFallbackPath` that `RewritingSink`
# then substitutes. Cold build seeds the store; `nix-build --check`
# rebuilds with the outputs still in place. `--check` exits 104
# because Apple's `ld` emits a non-deterministic `LC_UUID` the
# kernel requires, not because of our bug; we inspect the resulting
# `.check` tree regardless.

source common.sh

[[ $(uname) == Darwin ]] || skipTest "Mach-O page-hash rewriting is darwin-specific"
[[ -x /usr/bin/cc ]]       || skipTest "Need /usr/bin/cc to build the test fixture"
[[ -x /usr/bin/codesign ]] || skipTest "Need /usr/bin/codesign to validate signatures"
[[ -x /usr/bin/lipo ]]     || skipTest "Need /usr/bin/lipo to build fat Mach-O fixtures"
[[ -x /usr/bin/python3 ]]  || skipTest "Need /usr/bin/python3 to synthesize the CMS-skip fixture"

out=$(nix-build --no-out-link ./macho-rewrite.nix)
[[ -x "$out/bin/hello" ]]

if [[ -n "${NIX_TESTS_CA_BY_DEFAULT:-}" ]]; then
    # Confirm the fixture actually built as CA — `.info[].ca` is only
    # present for CA paths. Catches any silent regression where
    # `config.nix` stops honoring `NIX_TESTS_CA_BY_DEFAULT`.
    nix path-info --json --json-format 2 "$out" | jq -e '.info.[].ca' >/dev/null
    target="$out"
else
    expect 104 nix-build --no-out-link --check --keep-failed ./macho-rewrite.nix
    target="${out}.check"
    [[ -x "$target/bin/hello" ]]
fi

# dyld picks the host-matching slice for fat containers automatically.
# `hello-codesigned` exercises the flags=0x2 / 16 KiB-page signer;
# the rest are linker-signed.
for binary in hello hello-fat32-1arch hello-fat32-multi hello-codesigned; do
    path="$target/bin/$binary"
    [[ -x "$path" ]]

    output=$("$path")
    echo "$output" | grepQuiet -E "^doc=$NIX_STORE_DIR/[a-z0-9]{32}-macho-rewrite-multi-doc/share/doc/hello$"

    /usr/bin/codesign --verify "$path"
done

# Self-reference variant: the binary embeds its own $out path, which
# the daemon rewrites during the rebuild — same substitution
# mechanism as sibling-reference, different byte location.
[[ -x "$target/bin/hello-self" ]]
output=$("$target/bin/hello-self")
echo "$output" | grepQuiet -E "^self=$NIX_STORE_DIR/[a-z0-9]{32}-macho-rewrite-multi/bin/hello-self$"
/usr/bin/codesign --verify "$target/bin/hello-self"

# CMS-skip variant: the SuperBlob carries a synthetic non-empty
# `CSMAGIC_BLOBWRAPPER` under `CSSLOT_SIGNATURESLOT`. The helper's
# pre-scan must detect it and leave the slice untouched — recomputing
# the CodeDirectory would invalidate the (would-be) PKCS#7 chain.
#
# Assert on the specific error: `codesign --verify` must report "code
# or signature have been modified" (hash mismatch). A helper that
# wrongly recomputed slots would pass the hash check and fail later
# at the junk CMS blob with a different error, which a bare exit-code
# check would silently mask.
[[ -x "$target/bin/hello-cms" ]]
cms_verify=$(/usr/bin/codesign --verify -vv "$target/bin/hello-cms" 2>&1 || true)
echo "$cms_verify" | grepQuiet -F 'invalid signature (code or signature have been modified)'

# `codesign --verify` on a fat64 file reports "code object is not
# signed at all" even on a structurally-valid one on macOS 26; we
# verify per-slice after `lipo -thin` instead.
fat64="$target/lib/libgreet-fat64.dylib"
[[ -f "$fat64" ]]
# The fix-up never rewrites the fat header.
printf '\xca\xfe\xba\xbf' > "$TEST_ROOT/expected-fat64-magic"
cmp -n 4 "$TEST_ROOT/expected-fat64-magic" "$fat64"

work_fat64="$TEST_ROOT/fat64-slices"
mkdir -p "$work_fat64"
for arch in arm64 x86_64; do
    /usr/bin/lipo -thin "$arch" "$fat64" -output "$work_fat64/slice-$arch.dylib"
    /usr/bin/codesign --verify "$work_fat64/slice-$arch.dylib"
done

# `linker-signed` is set only by `ld`'s adhoc-codesign path; a
# refactor that re-signs via `codesign(1)` instead would clear it.
/usr/bin/codesign -dvvv "$target/bin/hello" 2>&1 | grepQuiet -F 'flags=0x20002(adhoc,linker-signed)'

# The helper must skip symlinks at both entry points.
[[ -L "$target/bin/hello-symlink" ]]

# Malformed fat-header fixtures — 32 bytes so they pass the helper's
# minimum-file-size check (28 bytes) and reach the nfat bound that
# we want to exercise. The helper must leave the bytes unchanged.
expected_dir="$TEST_ROOT/fixture-expected"
mkdir -p "$expected_dir"
{ printf '\xca\xfe\xba\xbe\x00\x00\x00\x00'; head -c 24 /dev/zero; } > "$expected_dir/nfat-zero"
{ printf '\xca\xfe\xba\xbe\x00\x00\x00\x41'; head -c 24 /dev/zero; } > "$expected_dir/java-class-shaped"
for fixture in nfat-zero java-class-shaped; do
    cmp "$expected_dir/$fixture" "$target/share/fixtures/$fixture"
done
