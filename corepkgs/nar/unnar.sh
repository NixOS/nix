#! /bin/sh

bunzip2 < $nar | /tmp/nix/bin/nix --restore "$out" || exit 1
