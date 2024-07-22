# shellcheck shell=bash

# Regression tests for the evaluator
# These are not in lang.sh because they generally only need to run in CI,
# whereas lang.sh is often run locally during development


source common.sh

set -o pipefail

skipTest "Too memory instensive for CI. Attempt to reduce memory usage was unsuccessful, because it made detection of the bug unreliable."

# Regression test for #11141. The stack pointer corrector assigned the base
# instead of the top (which resides at the low end of the stack). Sounds confusing?
# Stacks grow downwards, so that's why this mistake happened.
# My manual testing did not uncover this, because it didn't rely on the stack enough.
# https://github.com/NixOS/nix/issues/11141
test_issue_11141() {
  mkdir -p "$TEST_ROOT/issue-11141/src"
  cp lang-gc/issue-11141-gc-coroutine-test.nix "$TEST_ROOT/issue-11141/"
  (
    set +x;
    n=10
    echo "populating $TEST_ROOT/issue-11141/src with $((n*100)) files..."
    for i in $(seq 0 $n); do
      touch "$TEST_ROOT/issue-11141/src/file-$i"{0,1,2,3,4,5,6,7,8,9}{0,1,2,3,4,5,6,7,8,9}
    done
  )

  GC_INITIAL_HEAP_SIZE=$((1024 * 1024)) \
  NIX_SHOW_STATS=1 \
  nix eval -vvv\
    -f "$TEST_ROOT/issue-11141/issue-11141-gc-coroutine-test.nix"
}
test_issue_11141
