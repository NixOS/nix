source common.sh

drvPath=$($TOP/src/nix-instantiate/nix-instantiate locking.nix)

echo "derivation is $drvPath"

for i in 1 2 3 4 5; do
    echo "WORKER $i"
    $TOP/src/nix-store/nix-store -rvv "$drvPath" &
done

sleep 5

outPath=$($TOP/src/nix-store/nix-store -qvvf "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "aabcade"; then exit 1; fi
