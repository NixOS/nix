#!/bin/sh

exec nix-daemon --force-untrusted "$@"
