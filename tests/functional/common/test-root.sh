# shellcheck shell=bash

TEST_ROOT=$(realpath "${TMPDIR:-/tmp}/nix-test")/${TEST_NAME:-default/tests\/functional//}
export TEST_ROOT
