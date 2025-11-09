# shellcheck shell=bash

TEST_SUBDIR="${TEST_SUITE_NAME:-default}/${TEST_NAME:-tests/functional/}"
TEST_ROOT=$(realpath "${TMPDIR:-/tmp}/nix-test")/"$TEST_SUBDIR"
export TEST_ROOT
