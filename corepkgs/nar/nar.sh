#! /bin/sh

/tmp/nix/bin/nix --dump --file "$path" > $out || exit 1
