# Test that users cannot register specially-crafted derivations that
# produce output paths belonging to other derivations.  This could be
# used to inject malware into the store.

source common.sh

clearStore
clearManifests

startDaemon

# Determine the output path of the "good" derivation.
goodOut=$($nixstore -q $($nixinstantiate ./secure-drv-outputs.nix -A good))

# Instantiate the "bad" derivation.
badDrv=$($nixinstantiate ./secure-drv-outputs.nix -A bad)
badOut=$($nixstore -q $badDrv)

# Rewrite the bad derivation to produce the output path of the good
# derivation.
rm -f $TEST_ROOT/bad.drv
sed -e "s|$badOut|$goodOut|g" < $badDrv > $TEST_ROOT/bad.drv

# Add the manipulated derivation to the store and build it.  This
# should fail.
if badDrv2=$($nixstore --add $TEST_ROOT/bad.drv); then
    $nixstore -r "$badDrv2"
fi

# Now build the good derivation.
goodOut2=$($nixbuild ./secure-drv-outputs.nix -A good)
test "$goodOut" = "$goodOut2"

if ! test -e "$goodOut"/good; then
    echo "Bad derivation stole the output path of the good derivation!"
    exit 1
fi
