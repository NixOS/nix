# shellcheck shell=bash

set -eu -o pipefail

if [[ -z "${COMMON_VARS_SH_SOURCED-}" ]]; then

COMMON_VARS_SH_SOURCED=1

_NIX_TEST_SOURCE_DIR=$(realpath "${_NIX_TEST_SOURCE_DIR}")
_NIX_TEST_BUILD_DIR=$(realpath "${_NIX_TEST_BUILD_DIR}")

commonDir="$(readlink -f "$(dirname "${BASH_SOURCE[0]-$0}")")"

# Since this is a generated file
# shellcheck disable=SC1091
source "${_NIX_TEST_BUILD_DIR}/common/subst-vars.sh"
# Make sure shellcheck knows all these will be defined by the above generated snippet
: "${bindir?} ${coreutils?} ${dot?} ${SHELL?} ${busybox?} ${version?} ${system?}"
export coreutils dot busybox version system

export PAGER=cat

source "$commonDir/paths.sh"
source "$commonDir/test-root.sh"

test_nix_conf_dir=$TEST_ROOT/etc
# Used in other files
# shellcheck disable=SC2034
test_nix_conf=$test_nix_conf_dir/nix.conf

export TEST_HOME=$TEST_ROOT/test-home

if ! isTestOnNixOS; then
  export NIX_STORE_DIR
  if ! NIX_STORE_DIR=$(readlink -f "$TEST_ROOT/store" 2> /dev/null); then
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
unset GIT_DIR

export IMPURE_VAR1=foo
export IMPURE_VAR2=bar

# Used in other files
# shellcheck disable=SC2034
cacheDir=$TEST_ROOT/binary-cache

if [[ $(uname) == Linux ]] && [[ -L /proc/self/ns/user ]] && unshare --user true; then
    _canUseSandbox=1
fi

# Very common, shorthand helps
# Used in other files
# shellcheck disable=SC2034
config_nix="${_NIX_TEST_BUILD_DIR}/config.nix"

fi # COMMON_VARS_SH_SOURCED
