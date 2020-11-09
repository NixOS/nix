source common.sh

clearStore
clearCache

commonArgs=( \
    --experimental-features 'nix-command flakes ca-derivations ca-references' \
    --file ./content-addressed.nix \
    -v
)

remoteRoot=$TEST_ROOT/store2
chmod -R u+w "$remoteRoot" || true
rm -rf "$remoteRoot"

# Fill the remote cache
nix copy --to $remoteRoot --no-require-sigs "${commonArgs[@]}"
clearStore

# Fetch the otput from the cache
nix copy --from $remoteRoot --no-require-sigs "${commonArgs[@]}"

# Ensure that everything is locally present
nix build "${commonArgs[@]}" -j0 --no-link
