#! /bin/sh -e

DIFF=/home/eelco/Dev/nix/zdelta-2.1/zdc

srcA=$1
srcB=$2

if test -z "$srcA" -o -z "$srcB"; then
    echo "syntax: bdiff.sh srcA srcB"
    exit 1
fi

(cd $srcB && find . -type f) | while read fn; do

    echo "$fn" >&2

    if test -f "$srcA/$fn"; then

	echo "FILE DELTA FOR $fn"

	$DIFF "$srcA/$fn" "$srcB/$fn"

    else

	echo "NEW FILE $fn"

	cat "$srcB/$fn"

    fi

done
