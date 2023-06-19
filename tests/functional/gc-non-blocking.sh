# Test whether the collector is non-blocking, i.e. a build can run in
# parallel with it.
source common.sh

needLocalStore "the GC test needs a synchronisation point"

clearStore

fifo=$TEST_ROOT/test.fifo
mkfifo "$fifo"

fifo2=$TEST_ROOT/test2.fifo
mkfifo "$fifo2"

dummy=$(nix store add-path ./simple.nix)

running=$TEST_ROOT/running
touch $running

(_NIX_TEST_GC_SYNC=$fifo _NIX_TEST_GC_SYNC_2=$fifo2 nix-store --gc -vvvvv; rm $running) &
pid=$!

sleep 2

(sleep 1; echo > $fifo2) &

outPath=$(nix-build --max-silent-time 60 -o "$TEST_ROOT/result" -E "
  with import ./config.nix;
  mkDerivation {
    name = \"non-blocking\";
    buildCommand = \"set -x; test -e $running; mkdir \$out; echo > $fifo\";
  }")

wait $pid

(! test -e $running)
(! test -e $dummy)
test -e $outPath
