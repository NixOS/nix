# shellcheck shell=bash

# Remove overall test dir (at most one of the two should match) and
# remove file extension.

test_name=$(echo -n "${test?must be defined by caller (test runner)}" | sed \
    -e "s|^tests/unit/[^/]*/data/||" \
    -e "s|^tests/functional/||" \
    -e "s|\.sh$||" \
    )

# shellcheck disable=SC2016
TESTS_ENVIRONMENT=(
    "TEST_NAME=$test_name"
    'NIX_REMOTE='
    'PS4=+(${BASH_SOURCE[0]-$0}:$LINENO) '
)

read -r -a bash <<< "${BASH:-/usr/bin/env bash}"

run () {
   cd "$(dirname "$1")" && env "${TESTS_ENVIRONMENT[@]}" "${bash[@]}" -x -e -u -o pipefail "$(basename "$1")"
}
