source common.sh

clearStore

# Ensure "fake ssh" remote store works just as legacy fake ssh would.
nix --store ssh-ng://localhost?remote-store=$TEST_ROOT/other-store doctor

startDaemon

# Test import-from-derivation through the daemon.
[[ $(nix eval --impure --raw --expr '
  with import ./config.nix;
  import (
    mkDerivation {
      name = "foo";
      bla = import ./dependencies.nix;
      buildCommand = "
        echo \\\"hi\\\" > $out
      ";
    }
  )
') = hi ]]

storeCleared=1 NIX_REMOTE_=$NIX_REMOTE $SHELL ./user-envs.sh

nix-store --gc --max-freed 1K

nix-store --dump-db > $TEST_ROOT/d1
NIX_REMOTE= nix-store --dump-db > $TEST_ROOT/d2
cmp $TEST_ROOT/d1 $TEST_ROOT/d2

killDaemon
