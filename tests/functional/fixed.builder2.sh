# shellcheck shell=bash
# shellcheck disable=SC2154
echo dummy: "$dummy"
if test -n "$dummy"; then sleep 2; fi
# shellcheck disable=SC2154
mkdir "$out"
mkdir "$out"/bla
echo "Hello World!" > "$out"/foo
ln -s foo "$out"/bar
