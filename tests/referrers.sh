source common.sh

clearStore

max=2500

reference=$NIX_STORE_DIR/abcdef
touch $reference
(echo $reference && echo && echo 0) | $nixstore --register-validity 

echo "making registration..."

for ((n = 0; n < $max; n++)); do
    storePath=$NIX_STORE_DIR/$n
    echo -n > $storePath
    ref2=$NIX_STORE_DIR/$((n+1))
    if test $((n+1)) = $max; then
        ref2=$reference
    fi
    echo $storePath; echo; echo 2; echo $reference; echo $ref2
done > $TEST_ROOT/reg_info

echo "registering..."

time $nixstore --register-validity < $TEST_ROOT/reg_info

echo "collecting garbage..."
ln -sfn $reference "$NIX_STATE_DIR"/gcroots/ref
time $nixstore --gc

if test "$(sqlite3 ./test-tmp/db/db.sqlite 'select count(*) from Refs')" -ne 0; then
    echo "referrers not cleaned up"
    exit 1
fi
