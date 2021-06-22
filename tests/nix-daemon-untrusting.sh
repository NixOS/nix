#!/bin/sh

exec nix-daemon --no-trust "$@"
