#!/usr/bin/env bash

source ./common.sh

source ./user-envs-test-case.sh

# Suggestions quote derivation names, which are not format strings.
cat > "$TEST_ROOT"/percent.nix <<'NIX'
{
  a = {
    type = "derivation";
    name = "ab%1%c-1";
    outPath = "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-x";
  };
}
NIX
expectStderr 1 nix-env -f "$TEST_ROOT"/percent.nix -i ab | grepQuiet '^ *ab%1%c$'
