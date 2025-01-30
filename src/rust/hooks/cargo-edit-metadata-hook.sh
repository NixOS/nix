# shellcheck shell=bash

# Edit the `Cargo.toml` metadata:
# 1. Use the `version` from the `nix` app package (with some modifications to make it Cargo-legal)
# 2. Use the `description` from the `nix-rust-bridge` package
cargoEditMetadataHook() {
    local bridgeDescription="@bridgeDescription@"
    local bridgeVersion="@bridgeVersion@"
    local crateSubdir="@crateSubdir@"

    # Rewrite "<major>.<minor>.<patch>(preYYYYMMDD-dirty)?" as "<major>.<minor>.<patch>(-preYYYYMMDD.dirty)?"
    # Rewrite "<major>.<minor>.<patch>(preYYYYMMDD_<sha>)?" as "<major>.<minor>.<patch>(-preYYYYMMDD.<sha>)?"
    # shellcheck disable=SC2001
    bridgeVersion="$(echo ${bridgeVersion} | sed 's/^\([[:digit:]]\+\.[[:digit:]]\+\.[[:digit:]]\+\)\(.*\)$/\1-\2/g' | sed 's/_dirty$/.dirty/g' | sed 's/_\([[:alnum:]]\+\)/.\1/g')"

    pushd "crates/${crateSubdir}" || return 1
        # Edit the `Cargo.toml`
        sed -i "s/^version\s*=\s*\".*\"\$/version = \"${bridgeVersion}\"/g" Cargo.toml
        sed -i "s/^description\s*=\s*\".*\"\$/description = \"${bridgeDescription}\"/g" Cargo.toml

        # Propagate the edits to `Cargo.lock`
        cargo update --offline
    popd || return 1
}
