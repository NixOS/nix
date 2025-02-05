# shellcheck shell=bash

# Check that the Rust sources satisfy the clippy lints.
cargoClippyHook() {
    cargo clippy --all-targets --all-features --offline -- -D warnings
}
