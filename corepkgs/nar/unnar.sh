#! /bin/sh

echo "unpacking $nar to $out..."
bunzip2 < $nar | /nix/bin/nix --restore "$out" || exit 1
