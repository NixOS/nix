# shellcheck shell=bash

# Variables must be defined by caller, so
# shellcheck disable=SC2154

outPath=$(readlink -f "$TEST_ROOT/result")
grep 'FOO BAR BAZ' "${remoteDir}/${outPath}"
