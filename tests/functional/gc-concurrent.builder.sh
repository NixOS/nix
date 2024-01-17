echo "Build started" > "$lockFifo"

mkdir $out
echo $(cat $input1/foo)$(cat $input2/bar) > $out/foobar

# Wait for someone to write on the fifo
cat "$lockFifo"

# $out should not have been GC'ed while we were sleeping, but just in
# case...
mkdir -p $out

# Check that the GC hasn't deleted the lock on our output.
test -e "$out.lock"

ln -s $input2 $out/input-2
