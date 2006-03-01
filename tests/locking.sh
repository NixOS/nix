source common.sh

drvPath=$($nixinstantiate locking.nix)

echo "derivation is $drvPath"

for i in 1 2 3 4 5; do
    echo "WORKER $i"
    $nixstore -rvv "$drvPath" &
done

sleep 5

outPath=$($nixstore -qvvf "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath")
if test "$text" != "aabcade"; then exit 1; fi
