# Test that exporintg and importing a NAR file does not lose the ownership
# property stored in the NAR file.

source common.sh

clearStore

# Get the secret derivation rights.
secretOut=$(nix-build ./sec-secretDerivation.nix -A secret --no-out-link)

secret1OutStat=$(stat --format=%A $secretOut)
secret1OutFileStat=$(stat --format=%A $secretOut/file)

nix-store --export $(nix-store -qR $secretOut) > $TEST_ROOT/secret-data

clearStore

nix-store --import < $TEST_ROOT/secret-data

secret2OutStat=$(stat --format=%A $secretOut)
secret2OutFileStat=$(stat --format=%A $secretOut/file)

if ! test $secret1OutStat = $secret2OutStat ; then
    echo "Export/Import lose the ownership of the directory."
    exit 1
fi

if ! test $secret1OutFileStat = $secret2OutFileStat ; then
    echo "Export/Import lose the ownership of the content."
    exit 1
fi
