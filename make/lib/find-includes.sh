. $stdenv/setup

echo "finding includes of \`$(basename $main)'..."

makefile=$NIX_BUILD_TOP/makefile

mainDir=$(dirname $main)
(cd $mainDir && gcc $cFlags -MM $(basename $main) -MF $makefile) || false

echo "[" >$out

while read line; do
    line=$(echo "$line" | sed 's/.*://')
    for i in $line; do
        fullPath=$(readlink -f $mainDir/$i)
        echo "  [ $fullPath \"$i\" ]" >>$out
    done
done < $makefile

echo "]" >>$out
