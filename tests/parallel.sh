storeExpr=$($TOP/src/nix-instantiate/nix-instantiate parallel.nix)

echo "store expr is $storeExpr"

for i in $(seq 1 5); do
    echo "WORKER $i"
    $TOP/src/nix-store/nix-store -rvvB "$storeExpr" &
done

sleep 5

outPath=$($TOP/src/nix-store/nix-store -qnfvvvvv "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "aabcade"; then exit 1; fi

if test "$(cat $SHARED.cur)" != 0; then exit 1; fi
if test "$(cat $SHARED.max)" != 3; then exit 1; fi
