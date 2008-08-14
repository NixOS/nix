mkdir $out
echo $(cat $input1/foo)$(cat $input2/bar)xyzzy > $out/foobar

# Check that the GC hasn't deleted the lock on our output.
test -e "$out.lock"

sleep 6
