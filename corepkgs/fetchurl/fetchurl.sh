#! /bin/sh

echo "downloading $url into $out..."
wget "$url" -O "$out" || exit 1

actual=$(md5sum -b $out | cut -c1-32)
if ! test "$md5" == "ignore"; then
    if ! test "$actual" == "$md5"; then
	echo "hash is $actual, expected $md5"
	exit 1
    fi
fi
