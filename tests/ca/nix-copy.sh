source common.sh

clearStore
clearCache

commonArgs=( \
    --experimental-features 'nix-command flakes ca-derivations ca-references' \
    --file ./content-addressed.nix \
    -v
)

remoteRoot=$TEST_ROOT/store2

clearRemote() {
    chmod -R u+w "$remoteRoot" || true
    rm -rf "$remoteRoot"
}

backAndForth () {
    # Fill the remote cache (by copy-ing only the toplevel derivation outputs to
    # make sure that the dependencies are properly registered)
    nix copy --to $remoteRoot --no-require-sigs transitivelyDependentCA dependentNonCA dependentFixedOutput "${commonArgs[@]}"
    clearStore

    # Fetch the otput from the cache
    # First one derivation randomly choosen in dependency graph
    nix copy --from $remoteRoot --no-require-sigs "${commonArgs[@]}" dependentCA
    # Then everything
    nix copy --from $remoteRoot --no-require-sigs "${commonArgs[@]}"

    # Ensure that everything is locally present
    nix build "${commonArgs[@]}" -j0 --no-link
}

clearRemote
backAndForth
clearRemote
backAndForth
