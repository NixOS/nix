# shellcheck shell=bash

# Remove overall test dir (at most one of the two should match) and
# remove file extension.

test_name=$(echo -n "${test?must be defined by caller (test runner)}" | sed \
    -e "s|^src/[^/]*-test/data/||" \
    -e "s|^tests/functional/||" \
    -e "s|\.sh$||" \
    )

# Layer violation, but I am not inclined to care too much, as this code
# is about to be deleted.
src_dir=$(realpath tests/functional)

# shellcheck disable=SC2016
TESTS_ENVIRONMENT=(
    "TEST_NAME=$test_name"
    'NIX_REMOTE='
    'PS4=+(${BASH_SOURCE[0]-$0}:$LINENO) '
    "_NIX_TEST_SOURCE_DIR=${src_dir}"
    "_NIX_TEST_BUILD_DIR=${src_dir}"
)

unset src_dir

read -r -a bash <<< "${BASH:-/usr/bin/env bash}"

run () {
   cd "$(dirname "$1")" && env "${TESTS_ENVIRONMENT[@]}" "${bash[@]}" -x -e -u -o pipefail "$(basename "$1")"
}
