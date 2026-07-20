#!/usr/bin/env bash

source common.sh

# Regression test: `nix flake check --store <dest> --eval-store <eval>` must
# build the checks even though the destination store has never seen the
# freshly evaluated .drv files. queryMissing classifies such derivations as
# "unknown"; they used to be silently skipped, so a failing check "passed"
# without anything being built.

requireSandboxSupport
requiresUnprivilegedUserNamespaces
[[ "${busybox-}" =~ busybox ]] || skipTest "no busybox"

# Diverted stores need the logical store dir to not live inside the sandbox
# build dir (see build-remote.sh); every invocation below passes an explicit
# --store / --eval-store instead.
unset NIX_STORE_DIR

flakeDir=$TEST_ROOT/flake-check-store
evalStore=$TEST_ROOT/check-store-eval
mkdir -p "$flakeDir"

checkStore() {
    local destStore=$1
    local destStoreLocal=$2

    cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    checks.$system.failing = derivation {
      name = "failing-check";
      system = "$system";
      builder = "$busybox";
      args = [ "sh" "-e" (builtins.toFile "builder.sh" "echo the failing check ran; exit 1") ];
    };
  };
}
EOF

    # shellcheck disable=SC2015
    checkRes=$(nix flake check --store "$destStore" --eval-store "$evalStore" "$flakeDir" 2>&1 \
        && fail "nix flake check with a failing check should have failed against store $destStore" || true)
    echo "$checkRes" | grepQuiet "failing-check"

    cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = { self }: {
    checks.$system.passing = derivation {
      name = "passing-check";
      system = "$system";
      builder = "$busybox";
      args = [ "sh" "-e" (builtins.toFile "builder.sh" "echo ok > \$out") ];
    };
  };
}
EOF

    nix flake check --store "$destStore" --eval-store "$evalStore" "$flakeDir"

    # The check must have actually been built in the destination store.
    outPath=$(nix eval --store "$evalStore" --raw "$flakeDir#checks.$system.passing.outPath")
    nix path-info --store "$destStoreLocal" "$outPath"
    grepQuiet ok "$destStoreLocal/$outPath"
}

# Once against a local diverted store...
checkStore "$TEST_ROOT/check-store-dest" "$TEST_ROOT/check-store-dest"

# ...and once over the worker protocol (ssh-ng://localhost bypasses ssh and
# talks to a `nix daemon --stdio` child, testing RemoteStore::queryMissing
# and RemoteStore::buildPathsWithResults with an eval store).
checkStore "ssh-ng://localhost?remote-store=$TEST_ROOT/check-store-dest-ng" "$TEST_ROOT/check-store-dest-ng"
