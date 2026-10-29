#!/bin/sh

# shellcheck disable=SC2086
exec nix-daemon $_NIX_TEST_EXTRA_DAEMON_FLAGS "$@"
