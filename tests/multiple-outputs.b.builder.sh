mkdir $out
test "$firstOutput $secondOutput" = "$allOutputs"
test "$defaultOutput" = "$firstOutput"
test "$(cat $firstOutput/file)" = "second"
test "$(cat $secondOutput/file)" = "first"

echo "success" > $out/file
