set -e

if [[ -z "${COMMON_SH_SOURCED-}" ]]; then

COMMON_SH_SOURCED=1

source "$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")/common/vars-and-functions.sh"
if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]]; then
    startDaemon
fi

fi # COMMON_SH_SOURCED
