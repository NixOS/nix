#!/usr/bin/env bash

source common.sh

# The whole matrix of ways to override stdio trust.
cases=(
  ""
  "--default-trust"
  "--force-untrusted"
  "--force-trusted"
  "--process-ops"
  "--process-ops --default-trust"
  "--process-ops --force-untrusted"
  "--process-ops --force-trusted"
)

function testAllCombinations() {
  if isTrustedByAmbientStore; then
    ambientTrust=1
  else
    ambientTrust=0
  fi

  for mode in "${cases[@]}"; do
    # In all other cases the trust level must match that of the ambient store.
    if [[ "$mode" =~ .*--force-untrusted.* ]]; then
      expectedTrust=0
    else
      expectedTrust=$ambientTrust
    fi

    # Test both cli variants for consistency.
    for remoteProgram in $(readlink -e ./common/nix2-daemon-stdio.sh) $(readlink -e ./common/nix3-daemon-stdio.sh); do
      if (
        export NIX_REMOTE="ssh-ng://localhost?remote-program=$remoteProgram&remote-store=${1:-auto}"
        export _NIX_TEST_EXTRA_DAEMON_FLAGS="$mode"
        isTrustedByAmbientStore
      ); then
        actualTrust=1
      else
        actualTrust=0
      fi

      ((expectedTrust == actualTrust))
    done
  done
}

# If not in a NixOS test we can first test with a local store and then with a daemon.
testAllCombinations
if ! isTestOnNixOS; then
  # First as trusted user.
  startDaemon
  testAllCombinations "daemon"
  # And now as an untrusted user.
  sed -i -e '/^trusted-users =/d' "$test_nix_conf"
  restartDaemon
  testAllCombinations "daemon"
fi
