test_dir=tests/functional

test=$(echo -n "$test" | sed -e "s|^$test_dir/||")

TESTS_ENVIRONMENT=("TEST_NAME=${test%.*}" 'NIX_REMOTE=')

: ${BASH:=/usr/bin/env bash}

init_test () {
   cd "$test_dir" && env "${TESTS_ENVIRONMENT[@]}" $BASH -e init.sh 2>/dev/null > /dev/null
}

run_test_proper () {
   cd "$test_dir/$(dirname $test)" && env "${TESTS_ENVIRONMENT[@]}" $BASH -e $(basename $test)
}
