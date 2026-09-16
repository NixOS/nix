#!/usr/bin/env bash

source common.sh

TODO_NixOS

killDaemon
sed -i '/^trusted-users =/c\trusted-users =' "$NIX_CONF_DIR/nix.conf"
startDaemon

expectStderr 0 nix --store daemon \
    --option use-xdg-base-directories true \
    --option print-missing false store info \
    | grepQuietInverse 'ignoring the client-specified setting'

expectStderr 0 nix --store daemon \
    --option timeout 30 --option connect-timeout 5 \
    --option builders '' --option substituters '' store info \
    | grepQuietInverse 'ignoring'

expectStderr 0 nix --store daemon --option sandbox true store info \
    | grepQuiet "ignoring the client-specified setting 'sandbox'"

expectStderr 0 nix --store daemon --option builders 'ssh-ng://invalid.example' store info \
    | grepQuiet "ignoring the client-specified setting 'builders'"

expectStderr 0 nix --store daemon --option substituters 'https://invalid.example' store info \
    | grepQuiet 'ignoring untrusted substituter'

killDaemon
sed -i "s/^trusted-users =.*/trusted-users = $(whoami)/" "$NIX_CONF_DIR/nix.conf"
startDaemon

expectStderr 0 nix --store daemon --option sandbox false store info \
    | grepQuietInverse 'ignoring the client-specified setting'
