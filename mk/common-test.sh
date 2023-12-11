# Remove overall test dir (at most one of the two should match) and
# remove file extension.
test_name=$(echo -n "$test" | sed \
    -e "s|^tests/unit/[^/]*/data/||" \
    -e "s|^tests/functional/||" \
    -e "s|\.sh$||" \
    )

TESTS_ENVIRONMENT=(
    "TEST_NAME=$test_name"
    'NIX_REMOTE='
    'PS4=+(${BASH_SOURCE[0]-$0}:$LINENO) '
)

: ${BASH:=/usr/bin/env bash}

run () {
   cd "$(dirname $1)" && env "${TESTS_ENVIRONMENT[@]}" $BASH -x -e -u -o pipefail $(basename $1)
}

init_test () {
   run "$init" 2>/dev/null > /dev/null
}

run_test_proper () {
   run "$test"
}
