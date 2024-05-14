set -eu -o pipefail

if [[ -z "${COMMON_SH_SOURCED-}" ]]; then

COMMON_SH_SOURCED=1

dir="$(readlink -f "$(dirname "${BASH_SOURCE[0]-$0}")")"

source "$dir"/common/vars-and-functions.sh
source "$dir"/common/init.sh

if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]]; then
    startDaemon
fi

fi # COMMON_SH_SOURCED
