#! /bin/sh

/nix/bin/nix --dump --file "$path" | bzip2 > $out || exit 1
