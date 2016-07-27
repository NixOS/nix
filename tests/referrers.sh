source common.sh

clearStore

max=500

reference=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
touch $reference
(echo $reference && echo && echo 0) | nix-store --register-validity 

echo "making registration..."

set +x
for ((n = 0; n < $max; n++)); do
    storePath=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-$n
    echo -n > $storePath
    ref2=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-$((n+1))
    if test $((n+1)) = $max; then
        ref2=$reference
    fi
    echo $storePath; echo; echo 2; echo $reference; echo $ref2
done > $TEST_ROOT/reg_info
set -x

echo "registering..."

nix-store --register-validity < $TEST_ROOT/reg_info

echo "collecting garbage..."
ln -sfn $reference "$NIX_STATE_DIR"/gcroots/ref
nix-store --gc

if [ -n "$(type -p sqlite3)" -a "$(sqlite3 $NIX_STATE_DIR/db/db.sqlite 'select count(*) from Refs')" -ne 0 ]; then
    echo "referrers not cleaned up"
    exit 1
fi
