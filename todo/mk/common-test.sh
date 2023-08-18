TESTS_ENVIRONMENT=("TEST_NAME=${test%.*}" 'NIX_REMOTE=')

: ${BASH:=/usr/bin/env bash}

init_test () {
   cd tests && env "${TESTS_ENVIRONMENT[@]}" $BASH -e init.sh 2>/dev/null > /dev/null
}

run_test_proper () {
   cd $(dirname $test) && env "${TESTS_ENVIRONMENT[@]}" $BASH -e $(basename $test)
}
