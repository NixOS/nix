source common.sh

drvPath=$($nixinstantiate dependencies.nix)
outPath=$($nixstore -r $drvPath)

echo "pushing $drvPath"

mkdir $TEST_ROOT/cache

$PERL -w -I$TOP/scripts $TOP/scripts/nix-push \
    --copy $TEST_ROOT/cache $TEST_ROOT/manifest $drvPath
