source common.sh
# This test is run by `tests/nested-sandboxing/runner.nix` in an extra layer of sandboxing.
[[ -d /nix/store ]] || skipTest "running this test without Nix's deps being drawn from /nix/store is not yet supported"

requireSandboxSupport

source ./nested-sandboxing/command.sh

expectStderr 100 runNixBuild badStoreUrl 2 | grepQuiet '`sandbox-build-dir` must not contain'

runNixBuild goodStoreUrl 5
