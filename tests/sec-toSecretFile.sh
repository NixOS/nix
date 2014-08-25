# Test that toFile and toSecretFile are producing files with the right
# ownership, such as the secret file is never readable by other users.
#
# Ideally we should use inotify to ensure that the file is never created as
# being readable by others than the owner of the store.

source common.sh

clearStore
clearManifests

startDaemon

# Put both the public and secret files in the store.
nix-instantiate ./sec-toSecretFile.nix -A generateFiles

# Determine the output path of the public file.
publicFile=$(nix-instantiate ./sec-toSecretFile.nix -A public --eval-only | tr -d '"')
publicFileStat=$(stat --format=%A $publicFile)

# Determine the output path of the secret file.
secretFile=$(nix-instantiate ./sec-toSecretFile.nix -A secret --eval-only | tr -d '"')
secretFileStat=$(stat --format=%A $secretFile)

# Check file ownership, and verify that only the owner of the store is the
# only one capable of reading the secret file.
expected="-r--r--r--"
if ! test $publicFileStat = $expected; then
    echo "Public file should be readable by everybody."
    exit 1
fi

expected="-r--------"
if ! test $secretFileStat = $expected; then
    echo "Secret file should only be readable by root."
    exit 1
fi
