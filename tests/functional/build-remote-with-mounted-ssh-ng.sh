#!/usr/bin/env bash

source common.sh

requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"

# An example of a command that uses the store only for its settings, to
# make sure we catch needing the XP feature early.
touch "$TEST_ROOT/foo"
expectStderr 1 nix --store 'ssh-ng://localhost?mounted=%7B%7D' store add "$TEST_ROOT/foo" --dry-run | grepQuiet "experimental Nix feature 'mounted-ssh-store' is disabled"

enableFeatures mounted-ssh-store

# N.B. encoded query param is `mounted={}`. In the future, we can just
# do `--store` with JSON, and then the nested structure will actually
# bring benefits.
nix build -Lvf simple.nix \
  --arg busybox "$busybox" \
  --out-link "$TEST_ROOT/result-from-remote" \
  --store 'ssh-ng://localhost?mounted=%7B%7D'

nix build -Lvf simple.nix \
  --arg busybox "$busybox" \
  --out-link "$TEST_ROOT/result-from-remote-new-cli" \
  --store 'ssh-ng://localhost?mounted=%7B%7D&remote-program=nix daemon'

# This verifies that the out link was actually created and valid. The ability
# to create out links (permanent gc roots) is the distinguishing feature of
# the mounted-ssh-ng store.
grepQuiet 'Hello World!' < "$TEST_ROOT/result-from-remote/hello"
grepQuiet 'Hello World!' < "$TEST_ROOT/result-from-remote-new-cli/hello"
