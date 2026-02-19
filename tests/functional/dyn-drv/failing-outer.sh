#!/usr/bin/env bash

source common.sh

# Store layer needs bugfix
requireDaemonNewerThan "2.30pre20250515"

expected=100
if [[ -v NIX_DAEMON_PACKAGE ]] && ! isDaemonNewer "2.34pre"; then expected=1; fi # work around old daemons not returning a 100 status correctly

expectStderr "$expected" nix-build ./text-hashed-output.nix -A failingWrapper --no-out-link \
    | grepQuiet "build of resolved derivation '.*use-dynamic-drv-in-non-dynamic-drv-wrong.drv' failed"

# Test that error messages are not empty when a producer derivation fails.
# This exercises the nrFailed path in DerivationTrampolineGoal::init().
#
# Using `nix build --expr` with builtins.outputOf creates a top-level
# DerivationTrampolineGoal that goes through buildPathsWithResults.
# When the producer fails, the nrFailed path must use doneFailure (not amDone)
# to set buildResult.inner with a proper error message.
#
# Without the fix, the error message would be empty because amDone doesn't
# set buildResult.inner, so rethrow() throws Error("") - an empty message.

out=$(nix build --impure --no-link --expr '
  let
    config = import (builtins.getEnv "_NIX_TEST_BUILD_DIR" + "/config.nix");
    inherit (config) mkDerivation;

    # A CA derivation that fails before producing a .drv
    failingProducer = mkDerivation {
      name = "failing-producer";
      buildCommand = "echo This producer fails; exit 1";
      __contentAddressed = true;
      outputHashMode = "text";
      outputHashAlgo = "sha256";
    };
  in
  # Build the dynamic derivation output directly - this creates a top-level
  # DerivationTrampolineGoal, not a nested one inside a DerivationGoal
  builtins.outputOf failingProducer.outPath "out"
' 2>&1) || true

# Store layer needs bugfix
requireDaemonNewerThan "2.34pre"

# The error message must NOT be empty - it should mention the failed derivation
echo "$out" | grepQuiet "failed to obtain derivation of"
