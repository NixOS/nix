source common.sh

# This takes way to long on Cygwin (because process creation is so slow...).
if test "$system" = i686-cygwin; then exit 0; fi

max=2500

reference=$NIX_STORE_DIR/abcdef
touch $reference
(echo $reference && echo && echo 0) | $nixstore --register-validity 

echo "making registration..."

for ((n = 0; n < $max; n++)); do
    storePath=$NIX_STORE_DIR/$n
    touch $storePath
    ref2=$NIX_STORE_DIR/$((n+1))
    if test $((n+1)) = $max; then
        ref2=$reference
    fi
    (echo $storePath && echo && echo 2 && echo $reference && echo $ref2)
done > $TEST_ROOT/reg_info

echo "registering..."

time $nixstore --register-validity < $TEST_ROOT/reg_info

oldTime=$(cat test-tmp/db/info/1 | grep Registered-At)

echo "sleeping..."

sleep 2

echo "reregistering..."

time $nixstore --register-validity --reregister < $TEST_ROOT/reg_info

newTime=$(cat test-tmp/db/info/1 | grep Registered-At)

if test "$newTime" != "$oldTime"; then
    echo "reregistration changed original registration time"
    exit 1
fi

if test "$(cat test-tmp/db/referrer/1 | wc -w)" -ne 1; then
    echo "reregistration duplicated referrers"
    exit 1
fi

echo "collecting garbage..."
ln -sfn $reference "$NIX_STATE_DIR"/gcroots/ref
time $nixstore --gc

if test "$(cat test-tmp/db/referrer/abcdef | wc -w)" -ne 0; then
    echo "referrers not cleaned up"
    exit 1
fi

