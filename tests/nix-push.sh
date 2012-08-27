source common.sh

clearStore

drvPath=$(nix-instantiate dependencies.nix)
outPath=$(nix-store -r $drvPath)

echo "pushing $drvPath"

mkdir -p $TEST_ROOT/cache

nix-push --dest $TEST_ROOT/cache --manifest $drvPath --bzip2
