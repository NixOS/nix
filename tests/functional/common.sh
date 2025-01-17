# shellcheck shell=bash

set -eu -o pipefail

if [[ -z "${COMMON_SH_SOURCED-}" ]]; then

COMMON_SH_SOURCED=1

functionalTestsDir="$(readlink -f "$(dirname "${BASH_SOURCE[0]-$0}")")"

source "$functionalTestsDir/common/vars.sh"
source "$functionalTestsDir/common/functions.sh"
source "$functionalTestsDir/common/init.sh"

if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]]; then
    startDaemon
fi

fi # COMMON_SH_SOURCED
