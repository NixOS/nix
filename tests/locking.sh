storeExpr=$($TOP/src/nix-instantiate/nix-instantiate locking.nix)

echo "store expr is $storeExpr"

for i in 1 2 3 4 5; do
    echo "WORKER $i"
    $TOP/src/nix-store/nix-store -rvv "$storeExpr" &
done

sleep 5

outPath=$($TOP/src/nix-store/nix-store -qvvf "$storeExpr")

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "aabcade"; then exit 1; fi
