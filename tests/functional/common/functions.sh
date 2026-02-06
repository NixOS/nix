# shellcheck shell=bash

set -eu -o pipefail

if [[ -z "${COMMON_FUNCTIONS_SH_SOURCED-}" ]]; then

COMMON_FUNCTIONS_SH_SOURCED=1

isTestOnNixOS() {
  [[ "${isTestOnNixOS:-}" == 1 ]]
}

die() {
  echo "unexpected fatal error: $*" >&2
  exit 1
}

readLink() {
    # TODO fix this
    # shellcheck disable=SC2012
    ls -l "$1" | sed 's/.*->\ //'
}

clearProfiles() {
    profiles="$HOME/.local/state/nix/profiles"
    rm -rf "$profiles"
}

# Clear the store, but do not fail if we're in an environment where we can't.
# This allows the test to run in a NixOS test environment, where we use the system store.
# See doc/manual/source/contributing/testing.md / Running functional tests on NixOS.
clearStoreIfPossible() {
    if isTestOnNixOS; then
        echo "clearStoreIfPossible: Not clearing store, because we're on NixOS. Moving on."
    else
        doClearStore
    fi
}

clearStore() {
    if isTestOnNixOS; then
      die "clearStore: not supported when testing on NixOS. If not essential, call clearStoreIfPossible. If really needed, add conditionals; e.g. if ! isTestOnNixOS; then ..."
    fi
    doClearStore
}

doClearStore() {
    echo "clearing store..."
    chmod -R +w "$NIX_STORE_DIR"
    rm -rf "$NIX_STORE_DIR"
    mkdir "$NIX_STORE_DIR"
    rm -rf "$NIX_STATE_DIR"
    mkdir "$NIX_STATE_DIR"
    clearProfiles
}

clearCache() {
    rm -rf "${cacheDir?}"
}

clearCacheCache() {
    rm -f "$TEST_HOME/.cache/nix/binary-cache"*
}

startDaemon() {
    if isTestOnNixOS; then
      die "startDaemon: not supported when testing on NixOS. Is it really needed? If so add conditionals; e.g. if ! isTestOnNixOS; then ..."
    fi

    # Don't start the daemon twice, as this would just make it loop indefinitely.
    if [[ "${_NIX_TEST_DAEMON_PID-}" != '' ]]; then
        return
    fi
    # Start the daemon, wait for the socket to appear.
    rm -f "$NIX_DAEMON_SOCKET_PATH"
    PATH=$DAEMON_PATH nix --extra-experimental-features 'nix-command' daemon &
    _NIX_TEST_DAEMON_PID=$!
    export _NIX_TEST_DAEMON_PID
    for ((i = 0; i < 60; i++)); do
        if [[ -S $NIX_DAEMON_SOCKET_PATH ]]; then
          DAEMON_STARTED=1
          break;
        fi
        if ! kill -0 "$_NIX_TEST_DAEMON_PID"; then
          echo "daemon died unexpectedly" >&2
          exit 1
        fi
        sleep 0.1
    done
    if [[ -z ${DAEMON_STARTED+x} ]]; then
      fail "Didn't manage to start the daemon"
    fi
    trap "killDaemon" EXIT
    # Save for if daemon is killed
    NIX_REMOTE_OLD=$NIX_REMOTE
    export NIX_REMOTE=daemon
}

killDaemon() {
    if isTestOnNixOS; then
      die "killDaemon: not supported when testing on NixOS. Is it really needed? If so add conditionals; e.g. if ! isTestOnNixOS; then ..."
    fi

    # Don't fail trying to stop a non-existant daemon twice.
    if [[ "${_NIX_TEST_DAEMON_PID-}" == '' ]]; then
        return
    fi
    kill "$_NIX_TEST_DAEMON_PID"
    for i in {0..100}; do
        kill -0 "$_NIX_TEST_DAEMON_PID" 2> /dev/null || break
        sleep 0.1
    done
    kill -9 "$_NIX_TEST_DAEMON_PID" 2> /dev/null || true
    wait "$_NIX_TEST_DAEMON_PID" || true
    rm -f "$NIX_DAEMON_SOCKET_PATH"
    # Indicate daemon is stopped
    unset _NIX_TEST_DAEMON_PID
    # Restore old nix remote
    NIX_REMOTE=$NIX_REMOTE_OLD
    trap "" EXIT
}

restartDaemon() {
    if isTestOnNixOS; then
      die "restartDaemon: not supported when testing on NixOS. Is it really needed? If so add conditionals; e.g. if ! isTestOnNixOS; then ..."
    fi

    [[ -z "${_NIX_TEST_DAEMON_PID:-}" ]] && return 0

    killDaemon
    startDaemon
}

isDaemonNewer () {
  [[ -n "${NIX_DAEMON_PACKAGE:-}" ]] || return 0
  local requiredVersion="$1"
  local daemonVersion
  # Nix 2.4+ has unified 'nix' command; older versions only have nix-store etc.
  if [[ -x "$NIX_DAEMON_PACKAGE/bin/nix" ]]; then
    daemonVersion=$("$NIX_DAEMON_PACKAGE/bin/nix" daemon --version | cut -d' ' -f3)
  else
    daemonVersion=$("$NIX_DAEMON_PACKAGE/bin/nix-store" --version | cut -d' ' -f3)
  fi
  [[ $(nix eval --expr "builtins.compareVersions ''$daemonVersion'' ''$requiredVersion''") -ge 0 ]]
}

skipTest () {
    echo "$1, skipping this test..." >&2
    exit 77
}

TODO_NixOS() {
    if isTestOnNixOS; then
        skipTest "This test has not been adapted for NixOS yet"
    fi
}

requireDaemonNewerThan () {
    isDaemonNewer "$1" || skipTest "Daemon is too old"
}

canUseSandbox() {
    [[ ${_canUseSandbox-} ]]
}

requireSandboxSupport () {
    canUseSandbox || skipTest "Sandboxing not supported"
}

requireGit() {
    [[ $(type -p git) ]] || skipTest "Git not installed"
}

fail() {
    echo "test failed: $*" >&2
    exit 1
}

# Run a command failing if it didn't exit with the expected exit code.
#
# Has two advantages over the built-in `!`:
#
# 1. `!` conflates all non-0 codes. `expect` allows testing for an exact
# code.
#
# 2. `!` unexpectedly negates `set -e`, and cannot be used on individual
# pipeline stages with `set -o pipefail`. It only works on the entire
# pipeline, which is useless if we want, say, `nix ...` invocation to
# *fail*, but a grep on the error message it outputs to *succeed*.
expect() {
    local expected res
    expected="$1"
    shift
    "$@" && res=0 || res="$?"
    # also match "negative" codes, which wrap around to >127
    if [[ $res -ne $expected && $res -ne $((256 + expected)) ]]; then
        echo "Expected exit code '$expected' but got '$res' from command ${*@Q}" >&2
        return 1
    fi
    return 0
}

# Better than just doing `expect ... >&2` because the "Expected..."
# message below will *not* be redirected.
expectStderr() {
    local expected res
    expected="$1"
    shift
    "$@" 2>&1 && res=0 || res="$?"
    # also match "negative" codes, which wrap around to >127
    if [[ $res -ne $expected && $res -ne $((256 + expected)) ]]; then
        echo "Expected exit code '$expected' but got '$res' from command ${*@Q}" >&2
        return 1
    fi
    return 0
}

# Run a command and check whether the stderr matches stdin.
# Show a diff when output does not match.
# Usage:
#
#   assertStderr nix profile remove nothing << EOF
#   error: This error is expected
#   EOF
assertStderr() {
    diff -u /dev/stdin <("$@" 2>/dev/null 2>&1)
}

needLocalStore() {
  if [[ "$NIX_REMOTE" == "daemon" ]]; then
    skipTest "Can't run through the daemon ($1)"
  fi
}

# Just to make it easy to find which tests should be fixed
buggyNeedLocalStore() {
  needLocalStore "$1"
}

enableFeatures() {
    local features="$1"
    sed -i 's/experimental-features .*/& '"$features"'/' "${test_nix_conf?}"
}

onError() {
    set +x
    echo "$0: test failed at:" >&2
    for ((i = 1; i < ${#BASH_SOURCE[@]}; i++)); do
        if [[ -z ${BASH_SOURCE[i]} ]]; then break; fi
        echo "  ${FUNCNAME[i]} in ${BASH_SOURCE[i]}:${BASH_LINENO[i-1]}" >&2
    done
}

# Prints an error message prefix referring to the last call into this file.
# Ignores `expect` and `expectStderr` calls.
# Set a special exit code when test suite functions are misused, so that
# functions like expectStderr won't mistake them for expected Nix CLI errors.
# Suggestion: -101 (negative to indicate very abnormal, and beyond the normal
#             range of signals)
# Example (showns as string): 'repl.sh:123: in call to grepQuiet: '
# This function is inefficient, so it should only be used in error messages.
callerPrefix() {
  # Find the closest caller that's not from this file
  # using the bash `caller` builtin.
  local i file line fn savedFn
  # Use `caller`
  for i in $(seq 0 100); do
    caller "$i" > /dev/null || {
      if [[ -n "${file:-}" ]]; then
        echo "$file:$line: ${savedFn+in call to $savedFn: }"
      fi
      break
    }
    line="$(caller "$i" | cut -d' ' -f1)"
    fn="$(caller "$i" | cut -d' ' -f2)"
    file="$(caller "$i" | cut -d' ' -f3)"
    if [[ $file != "${BASH_SOURCE[0]}" ]]; then
      echo "$file:$line: ${savedFn+in call to $savedFn: }"
      return
    fi
    case "$fn" in
      # Ignore higher order functions that don't report any misuse of themselves
      # This way a misuse of a foo in `expectStderr 1 foo` will be reported as
      # calling foo, not expectStderr.
      expect|expectStderr|callerPrefix)
        ;;
      *)
        savedFn="$fn"
        ;;
    esac
  done
}

checkGrepArgs() {
    local arg
    for arg in "$@"; do
        if [[ "$arg" != "${arg//$'\n'/_}" ]]; then
            echo "$(callerPrefix)newline not allowed in arguments; grep would try each line individually as if connected by an OR operator" >&2
            return 155 # = -101 mod 256
        fi
    done
}

# `grep -v` doesn't work well for exit codes. We want `!(exist line l. l
# matches)`. It gives us `exist line l. !(l matches)`.
#
# `!` normally doesn't work well with `set -e`, but when we wrap in a
# function it *does*.
#
# `command grep` lets us avoid re-checking the args by going directly to the
# executable.
grepInverse() {
    checkGrepArgs "$@" && \
      ! command grep "$@"
}

# A shorthand, `> /dev/null` is a bit noisy.
#
# `grep -q` would seem to do this, no function necessary, but it is a
# bad fit with pipes and `set -o pipefail`: `-q` will exit after the
# first match, and then subsequent writes will result in broken pipes.
#
# Note that reproducing the above is a bit tricky as it depends on
# non-deterministic properties such as the timing between the match and
# the closing of the pipe, the buffering of the pipe, and the speed of
# the producer into the pipe. But rest assured we've seen it happen in
# CI reliably.
#
# `command grep` lets us avoid re-checking the args by going directly to the
# executable.
grepQuiet() {
    checkGrepArgs "$@" && \
      command grep "$@" > /dev/null
}

# The previous two, combined
grepQuietInverse() {
    checkGrepArgs "$@" && \
      ! command grep "$@" > /dev/null
}

# Wrap grep to remove its newline footgun; see checkGrepArgs.
# Note that we keep the checkGrepArgs calls in the other helpers, because some
# of them are negated and that would defeat this check.
grep() {
    checkGrepArgs "$@" && \
      command grep "$@"
}

# Return the number of arguments
count() {
  echo $#
}

trap onError ERR

requiresUnprivilegedUserNamespaces() {
  if [[ -f /proc/sys/kernel/apparmor_restrict_unprivileged_userns ]] && [[ $(< /proc/sys/kernel/apparmor_restrict_unprivileged_userns) -eq 1 ]]; then
    skipTest "Unprivileged user namespaces are disabled. Run 'sudo sysctl -w /proc/sys/kernel/apparmor_restrict_unprivileged_userns=0' to allow, and run these tests."
  fi
}

execUnshare () {
  requiresUnprivilegedUserNamespaces
  exec unshare --mount --map-root-user "$SHELL" "$@"
}

initGitRepo() {
    local repo="$1"
    local extraArgs="${2-}"

    # shellcheck disable=SC2086 # word splitting of extraArgs is intended
    git -C "$repo" init $extraArgs
    git -C "$repo" config user.email "foobar@example.com"
    git -C "$repo" config user.name "Foobar"
}

createGitRepo() {
    local repo="$1"
    local extraArgs="${2-}"

    rm -rf "$repo" "$repo".tmp
    mkdir -p "$repo"

    # shellcheck disable=SC2086 # word splitting of extraArgs is intended
    initGitRepo "$repo" $extraArgs
}

fi # COMMON_FUNCTIONS_SH_SOURCED
