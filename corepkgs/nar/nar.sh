#! /bin/sh

echo "packing $path into $out..."
/nix/bin/nix --dump --file "$path" | bzip2 > $out || exit 1

