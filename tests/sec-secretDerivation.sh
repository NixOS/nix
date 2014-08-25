# Test that derivation and secret derivations are producing *.drv files with
# the right ownership, such as the secret file is never readable by other
# users.
#
# Ideally we should use inotify to ensure that the file is never created as
# being readable by others than the owner of the store.

source common.sh

clearStore
clearManifests

startDaemon

# Get the public derivation rights.
publicDrv=$(nix-instantiate ./sec-secretDerivation.nix -A public)
publicDrvStat=$(stat --format=%A $publicDrv)
publicOut=$(nix-store -rvv $publicDrv)
publicOutStat=$(stat --format=%A $publicOut)
publicOutFileStat=$(stat --format=%A $publicOut/file)

# Get the secret derivation rights.
secretDrv=$(nix-instantiate ./sec-secretDerivation.nix -A secret)
secretDrvStat=$(stat --format=%A $secretDrv)
secretOut=$(nix-store -rvv $secretDrv)
secretOutStat=$(stat --format=%A $secretOut)
secretOutFileStat=$(stat --format=%A $secretOut/file)

# Check file ownership, and verify that only the owner of the store is the
# only one capable of reading the secret file.
expected="-r--r--r--"
if ! test $publicDrvStat = $expected; then
    echo "Public derivation should be readable by everybody."
    exit 1
fi

expected="-r--------"
if ! test $secretDrvStat = $expected; then
    echo "Secret derivation should only be readable by root."
    exit 1
fi
