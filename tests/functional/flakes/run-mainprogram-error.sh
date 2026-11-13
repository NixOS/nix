#!/usr/bin/env bash

source ../common.sh

TODO_NixOS

# Test that when a package has no meta.mainProgram and the inferred binary
# doesn't exist, the original error is preserved and a trace hints at the cause.
clearStore
rm -rf "$TEST_HOME"/.cache "$TEST_HOME"/.config "$TEST_HOME"/.local

cp ../no-mainprogram.nix ../no-mainprogram-name-only.nix "${config_nix}" "$TEST_HOME"
cd "$TEST_HOME"

cat <<EOF > flake.nix
{
  outputs = { self }: {
    packages.$system.default = (import ./no-mainprogram.nix);
  };
}
EOF

# Should fail with traces showing provenance and a meta.mainProgram hint,
# with the original SysError preserved.
stderr=$(expectStderr 1 nix run --no-write-lock-file .)
echo "$stderr" | grepQuiet "while running program 'multi-tool'"
echo "$stderr" | grepQuiet "determined from 'pname'"
echo "$stderr" | grepQuiet "consider setting 'meta.mainProgram' or using 'nix shell'"
echo "$stderr" | grepQuiet "unable to execute"

# Also test the 'name' provenance case (no pname, no meta.mainProgram).
cat <<EOF > flake.nix
{
  outputs = { self }: {
    packages.$system.default = (import ./no-mainprogram-name-only.nix);
  };
}
EOF

stderr=$(expectStderr 1 nix run --no-write-lock-file .)
echo "$stderr" | grepQuiet "while running program 'mytool'"
echo "$stderr" | grepQuiet "determined from 'name'"
echo "$stderr" | grepQuiet "consider setting 'meta.mainProgram' or using 'nix shell'"
echo "$stderr" | grepQuiet "unable to execute"
