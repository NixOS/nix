# Test that users cannot register specially-crafted derivations that
# produce output paths belonging to other derivations.  This could be
# used to inject malware into the store.

source common.sh

clearStore

startDaemon

# Determine the output path of the "good" derivation.
goodOut=$(nix-store -q $(nix-instantiate ./secure-drv-outputs.nix -A good))

# Instantiate the "bad" derivation.
badDrv=$(nix-instantiate ./secure-drv-outputs.nix -A bad)
badOut=$(nix-store -q $badDrv)

# Rewrite the bad derivation to produce the output path of the good
# derivation.
rm -f $TEST_ROOT/bad.drv
sed -e "s|$badOut|$goodOut|g" < $badDrv > $TEST_ROOT/bad.drv

# Add the manipulated derivation to the store and build it.  This
# should fail.
if badDrv2=$(nix-store --add $TEST_ROOT/bad.drv); then
    nix-store -r "$badDrv2"
fi

# Now build the good derivation.
goodOut2=$(nix-build ./secure-drv-outputs.nix -A good --no-out-link)
test "$goodOut" = "$goodOut2"

if ! test -e "$goodOut"/good; then
    echo "Bad derivation stole the output path of the good derivation!"
    exit 1
fi
