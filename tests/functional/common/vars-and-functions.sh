# NOTE: instances of @variable@ are substituted as defined in /mk/templates.mk

set -eu -o pipefail

if [[ -z "${COMMON_VARS_AND_FUNCTIONS_SH_SOURCED-}" ]]; then

COMMON_VARS_AND_FUNCTIONS_SH_SOURCED=1

isTestOnNixOS() {
  [[ "${isTestOnNixOS:-}" == 1 ]]
}

die() {
  echo "unexpected fatal error: $*" >&2
  exit 1
}

set +x

commonDir="$(readlink -f "$(dirname "${BASH_SOURCE[0]-$0}")")"

source "$commonDir/subst-vars.sh"
# Make sure shellcheck knows all these will be defined by the above generated snippet
: "${bindir?} ${coreutils?} ${dot?} ${SHELL?} ${PAGER?} ${busybox?} ${version?} ${system?} ${BUILD_SHARED_LIBS?}"

source "$commonDir/paths.sh"
source "$commonDir/test-root.sh"

test_nix_conf_dir=$TEST_ROOT/etc
test_nix_conf=$test_nix_conf_dir/nix.conf

# introduced in master in 9d2ed0a7d384a7759d5981425fba41ecf1b4b9e1,
# https://github.com/NixOS/nix/pull/11792. Could be replaced by a
# full backport.
config_nix="${commonDir}/config.nix"

export TEST_HOME=$TEST_ROOT/test-home

if ! isTestOnNixOS; then
  export NIX_STORE_DIR
  if ! NIX_STORE_DIR=$(readlink -f $TEST_ROOT/store 2> /dev/null); then
      # Maybe the build directory is symlinked.
      export NIX_IGNORE_SYMLINK_STORE=1
      NIX_STORE_DIR=$TEST_ROOT/store
  fi
  export NIX_LOCALSTATE_DIR=$TEST_ROOT/var
  export NIX_LOG_DIR=$TEST_ROOT/var/log/nix
  export NIX_STATE_DIR=$TEST_ROOT/var/nix
  export NIX_CONF_DIR=$test_nix_conf_dir
  export NIX_DAEMON_SOCKET_PATH=$TEST_ROOT/dSocket
  unset NIX_USER_CONF_FILES
  export _NIX_TEST_SHARED=$TEST_ROOT/shared
  if [[ -n $NIX_STORE ]]; then
      export _NIX_TEST_NO_SANDBOX=1
  fi
  export _NIX_IN_TEST=$TEST_ROOT/shared
  export _NIX_TEST_NO_LSOF=1
  export NIX_REMOTE=${NIX_REMOTE_-}

fi # ! isTestOnNixOS

unset NIX_PATH
export HOME=$TEST_HOME
unset XDG_STATE_HOME
unset XDG_DATA_HOME
unset XDG_CONFIG_HOME
unset XDG_CONFIG_DIRS
unset XDG_CACHE_HOME

export IMPURE_VAR1=foo
export IMPURE_VAR2=bar

cacheDir=$TEST_ROOT/binary-cache

readLink() {
    ls -l "$1" | sed 's/.*->\ //'
}

clearProfiles() {
    profiles="$HOME"/.local/state/nix/profiles
    rm -rf "$profiles"
}

# Clear the store, but do not fail if we're in an environment where we can't.
# This allows the test to run in a NixOS test environment, where we use the system store.
# See doc/manual/src/contributing/testing.md / Running functional tests on NixOS.
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
    rm -rf "$cacheDir"
}

clearCacheCache() {
    rm -f $TEST_HOME/.cache/nix/binary-cache*
}

startDaemon() {
    if isTestOnNixOS; then
      die "startDaemon: not supported when testing on NixOS. Is it really needed? If so add conditionals; e.g. if ! isTestOnNixOS; then ..."
    fi

    # Don’t start the daemon twice, as this would just make it loop indefinitely
    if [[ "${_NIX_TEST_DAEMON_PID-}" != '' ]]; then
        return
    fi
    # Start the daemon, wait for the socket to appear.
    rm -f $NIX_DAEMON_SOCKET_PATH
    PATH=$DAEMON_PATH nix --extra-experimental-features 'nix-command' daemon &
    _NIX_TEST_DAEMON_PID=$!
    export _NIX_TEST_DAEMON_PID
    for ((i = 0; i < 300; i++)); do
        if [[ -S $NIX_DAEMON_SOCKET_PATH ]]; then
          DAEMON_STARTED=1
          break;
        fi
        sleep 0.1
    done
    if [[ -z ${DAEMON_STARTED+x} ]]; then
      fail "Didn’t manage to start the daemon"
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

    # Don’t fail trying to stop a non-existant daemon twice
    if [[ "${_NIX_TEST_DAEMON_PID-}" == '' ]]; then
        return
    fi
    kill $_NIX_TEST_DAEMON_PID
    for i in {0..100}; do
        kill -0 $_NIX_TEST_DAEMON_PID 2> /dev/null || break
        sleep 0.1
    done
    kill -9 $_NIX_TEST_DAEMON_PID 2> /dev/null || true
    wait $_NIX_TEST_DAEMON_PID || true
    rm -f $NIX_DAEMON_SOCKET_PATH
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

if [[ $(uname) == Linux ]] && [[ -L /proc/self/ns/user ]] && unshare --user true; then
    _canUseSandbox=1
fi

isDaemonNewer () {
  [[ -n "${NIX_DAEMON_PACKAGE:-}" ]] || return 0
  local requiredVersion="$1"
  local daemonVersion=$($NIX_DAEMON_PACKAGE/bin/nix daemon --version | cut -d' ' -f3)
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
    if [[ $res -ne $expected && $res -ne $[256 + expected] ]]; then
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
    if [[ $res -ne $expected && $res -ne $[256 + expected] ]]; then
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
    diff -u /dev/stdin <($@ 2>/dev/null 2>&1)
}

needLocalStore() {
  if [[ "$NIX_REMOTE" == "daemon" ]]; then
    skipTest "Can’t run through the daemon ($1)"
  fi
}

# Just to make it easy to find which tests should be fixed
buggyNeedLocalStore() {
  needLocalStore "$1"
}

enableFeatures() {
    local features="$1"
    sed -i 's/experimental-features .*/& '"$features"'/' "$test_nix_conf_dir"/nix.conf
}

set -x

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
    caller $i > /dev/null || {
      if [[ -n "${file:-}" ]]; then
        echo "$file:$line: ${savedFn+in call to $savedFn: }"
      fi
      break
    }
    line="$(caller $i | cut -d' ' -f1)"
    fn="$(caller $i | cut -d' ' -f2)"
    file="$(caller $i | cut -d' ' -f3)"
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
            return -101
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

fi # COMMON_VARS_AND_FUNCTIONS_SH_SOURCED
