# shellcheck shell=bash

# Check that the Rust sources are properly formatted.
cargoFmtCheckHook() {
    cargo-fmt-nightly --all --check
}
