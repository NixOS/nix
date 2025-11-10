# shellcheck shell=bash
proto=$1
shift
(( $# == 0 ))

TODO_NixOS

clearStore
clearCache

mkdir -p "$TEST_ROOT"/stores

# Create path to copy back and forth
outPath=$(nix-build --no-out-link dependencies.nix)

storeQueryParam="store=${NIX_STORE_DIR}"

realQueryParam () {
    echo "real=$1$NIX_STORE_DIR"
}

remoteRoot="$TEST_ROOT/stores/$proto"

clearRemoteStore () {
    chmod -R u+w "$remoteRoot" || true
    rm -rf "$remoteRoot"
}

clearRemoteStore

remoteStore="${proto}://localhost?${storeQueryParam}&remote-store=${remoteRoot}%3f${storeQueryParam}%26$(realQueryParam "$remoteRoot")"

# Copy to store

args=()
if [[ "$proto" == "ssh-ng" ]]; then
    # TODO investigate discrepancy
    args+=(--no-check-sigs)
fi

[ ! -f "${remoteRoot}""${outPath}"/foobar ]
nix copy "${args[@]}" --to "$remoteStore" "$outPath"
[ -f "${remoteRoot}""${outPath}"/foobar ]

# Copy back from store

clearStore

[ ! -f "$outPath"/foobar ]
nix copy --no-check-sigs --from "$remoteStore" "$outPath"
[ -f "$outPath"/foobar ]

# Check --substitute-on-destination, avoid corrupted store

clearRemoteStore

corruptedRoot=$TEST_ROOT/stores/corrupted
corruptedStore="${corruptedRoot}?${storeQueryParam}&$(realQueryParam "$corruptedRoot")"

# Copy it to the corrupted store
nix copy --no-check-sigs "$outPath" --to "$corruptedStore"

# Corrupt it in there
corruptPath="${corruptedRoot}${outPath}"
chmod +w "$corruptPath"
echo "not supposed to be here" > "$corruptPath/foobarbaz"
chmod -w "$corruptPath"

# Copy from the corrupted store with the regular store as a
# substituter. It must use the substituter not the source store in
# order to avoid errors.
NIX_CONFIG=$(echo -e "substituters = local\nrequire-sigs = false") \
    nix copy --no-check-sigs --from "$corruptedStore" --to "$remoteStore" --substitute-on-destination "$outPath"
