if test "$IMPURE_VAR1" != "foo"; then exit 1; fi
if test "$IMPURE_VAR2" != "bar"; then exit 1; fi
echo "Hello World!" > $out
