# shellcheck shell=bash

# Copy `cxxbridge` (with symlinks) directories from Rust target directory to $out
cxxCopyTargetBridgesHook() {
    local dev=$1
    local cxxbridge=$2

    if [[ -z "${dev}" ]]; then
        echo "${FUNCNAME[0]}: required argument '$dev' missing"
        return 1
    fi

    if [[ -z "${cxxbridge}" ]]; then
        echo "${FUNCNAME[0]}: required argument '$cxxbridge' missing"
        return 1
    fi

    # Recursively copy `cxxbridge` and resolve symlinks
    pushd target/cxxbridge || return 1
        mkdir -p "${dev}/include"
        find . -name '*.h' -type l -exec cp --archive --dereference --parents --recursive {} "${dev}/include" \;
        mkdir -p "${cxxbridge}/cxxbridge"
        find . -name '*.cc' -type l -exec cp --archive --dereference --parents --recursive {} "${cxxbridge}/cxxbridge" \;
    popd || return 1
}
