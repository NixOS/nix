# shellcheck shell=bash

set -eu -o pipefail

if [[ -z "${COMMON_PATHS_SH_SOURCED-}" ]]; then

COMMON_PATHS_SH_SOURCED=1

commonDir="$(readlink -f "$(dirname "${BASH_SOURCE[0]-$0}")")"

# Just for `isTestOnNixOS`
source "$commonDir/functions.sh"
# shellcheck disable=SC1091
source "${_NIX_TEST_BUILD_DIR}/common/subst-vars.sh"
# Make sure shellcheck knows this will be defined by the above generated snippet
: "${bash?}" "${bindir?}"

if ! isTestOnNixOS; then
  export SHELL="$bash"
  export PATH="$bindir:$PATH"
fi

if [[ -n "${NIX_CLIENT_PACKAGE:-}" ]]; then
  export PATH="$NIX_CLIENT_PACKAGE/bin":$PATH
fi

DAEMON_PATH="$PATH"
if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]]; then
  DAEMON_PATH="${NIX_DAEMON_PACKAGE}/bin:$DAEMON_PATH"
fi

fi # COMMON_PATHS_SH_SOURCED
