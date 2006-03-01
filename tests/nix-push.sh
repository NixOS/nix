source common.sh

drvPath=$($TOP/src/nix-instantiate/nix-instantiate dependencies.nix)
outPath=$($TOP/src/nix-store/nix-store -r $drvPath)

echo "pushing $drvPath"

mkdir $TEST_ROOT/cache

$PERL -w -I$TOP/scripts $TOP/scripts/nix-push \
    --copy $TEST_ROOT/cache $TEST_ROOT/manifest $drvPath
