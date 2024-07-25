# shellcheck shell=bash

commonDir="$(readlink -f "$(dirname "${BASH_SOURCE[0]-$0}")")"

# Since this is a generated file
# shellcheck disable=SC1091
source "$commonDir/subst-vars.sh"
# Make sure shellcheck knows this will be defined by the above generated snippet
: "${bindir?}"

export PATH="$bindir:$PATH"

if [[ -n "${NIX_CLIENT_PACKAGE:-}" ]]; then
  export PATH="$NIX_CLIENT_PACKAGE/bin":$PATH
fi

DAEMON_PATH="$PATH"
if [[ -n "${NIX_DAEMON_PACKAGE:-}" ]]; then
  DAEMON_PATH="${NIX_DAEMON_PACKAGE}/bin:$DAEMON_PATH"
fi
