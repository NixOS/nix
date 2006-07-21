mkdir $out
echo $(cat $input1/foo)$(cat $input2/bar) > $out/foobar

sleep 5
mkdir $out || true

# Check that the GC hasn't deleted the lock on our output.
test -e "$out.lock"

ln -s $input2 $out/input-2
