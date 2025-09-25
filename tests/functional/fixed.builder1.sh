# shellcheck shell=bash
if test "$IMPURE_VAR1" != "foo"; then exit 1; fi
if test "$IMPURE_VAR2" != "bar"; then exit 1; fi
# shellcheck disable=SC2154
echo "Hello World!" > "$out"
