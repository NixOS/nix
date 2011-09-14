mkdir $out
test "$firstOutput $secondOutput" = "$allOutputs"
test "$defaultOutput" = "$firstOutput"
test "$(cat $first/file)" = "second"
test "$(cat $second/file)" = "first"

echo "success" > $out/file
