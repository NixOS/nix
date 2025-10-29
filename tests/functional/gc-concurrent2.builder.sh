# shellcheck shell=bash
# shellcheck disable=SC2154
mkdir "$out"
# shellcheck disable=SC2154
echo "$(cat "$input1"/foo)""$(cat "$input2"/bar)"xyzzy > "$out"/foobar

# Check that the GC hasn't deleted the lock on our output.
test -e "$out.lock"
