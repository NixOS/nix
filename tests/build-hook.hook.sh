set -x

drv=$1

echo "HOOK for $drv"

outPath=$(sed 's/Derive(\[\"\([^\"]*\)\".*/\1/' $drv)

echo "output path is $outPath"

if $(echo $outPath | grep -q input-1); then
    mkdir $outPath
    echo "BAR" > $outPath/foo
    exit 100
fi

exit 101