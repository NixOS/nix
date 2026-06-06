# shellcheck shell=bash

# All variables should be defined externally by the scripts that source this.
# shellcheck disable=SC2154

legacySshAddToStoreFailureOutput() {
    TODO_NixOS

    clearStore
    clearCache

    mkdir -p "$TEST_ROOT/stores"

    local outPath
    outPath=$(nix-build --no-out-link dependencies.nix)

    local storeQueryParam
    storeQueryParam="store=${NIX_STORE_DIR}"

    local remoteRoot
    remoteRoot="$TEST_ROOT/stores/legacy-ssh-no-write"
    chmod -R u+w "$remoteRoot" || true
    rm -rf "$remoteRoot"

    local remoteProgram
    if [[ -n "${src-}" && -x "$src/tests/functional/legacy-ssh-store-no-write.sh" ]]; then
        remoteProgram="$src/tests/functional/legacy-ssh-store-no-write.sh"
    else
        remoteProgram="$TEST_ROOT/nix-store-no-write.sh"
        cp "$functionalTestsDir/legacy-ssh-store-no-write.sh" "$remoteProgram"
        chmod +x "$remoteProgram"
    fi

    local remoteStore
    remoteStore="ssh://localhost?${storeQueryParam}&remote-program=${remoteProgram}&remote-store=${remoteRoot}%3f${storeQueryParam}%26real=${remoteRoot}${NIX_STORE_DIR}"

    expectStderr 1 nix copy --to "$remoteStore" "$outPath"
}
