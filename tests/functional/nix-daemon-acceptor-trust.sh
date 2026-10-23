#!/usr/bin/env bash

source common.sh

# Note that we are testing our current package's daemon, not us against a possibly different daemon.
# We'll have our daemon forward to another daemon (or local store).

# The whole matrix of ways to override acceptor trust. --process-ops is the only behavior of the acceptor
# currently and there's no `--no-process-ops` variant.
cases=(
  ""
  "--default-trust"
  "--force-untrusted"
  "--force-trusted"
)

sockpath="$TEST_ROOT/dSocketUnderTest"

function testAllCombinations() {
  local ambientTrust

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

    rm -f "$sockpath"
    # shellcheck disable=SC2086
    nix --log-format raw daemon --socket-path "$sockpath" $mode 2>> "$TEST_ROOT/daemon-log-under-test" &
    local _NIX_TEST_DAEMON_PID_UNDER_TEST=$!

    for ((i = 0; i < 60; i++)); do
      if [[ -S $sockpath ]]; then
        break;
      fi
      sleep 0.1
    done

    local actualTrust

    if NIX_REMOTE="unix://$sockpath" isTrustedByAmbientStore; then
      actualTrust=1
    else
      actualTrust=0
    fi

    # Handling the traps it probably a bit too annoying.
    kill -9 $_NIX_TEST_DAEMON_PID_UNDER_TEST
    wait $_NIX_TEST_DAEMON_PID_UNDER_TEST || true

    ((expectedTrust == actualTrust))
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
