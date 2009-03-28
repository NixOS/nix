#! /bin/sh

#set -x

drv=$4

echo "HOOK for $drv" >&2

outPath=$(sed 's/Derive(\[("out",\"\([^\"]*\)\".*/\1/' $drv)

echo "output path is $outPath" >&2

if $(echo $outPath | grep -q input-1); then
    echo "# accept" >&2
    read x
    echo "got $x"
    mkdir $outPath
    echo "BAR" > $outPath/foo
else
    echo "# decline" >&2
fi
