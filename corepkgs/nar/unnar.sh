#! /bin/sh

/tmp/nix/bin/nix --restore "$out" < $nar || exit 1
