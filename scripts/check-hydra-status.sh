#!/usr/bin/env bash

set -euo pipefail
# set -x


# mapfile BUILDS_FOR_LATEST_EVAL < <(
# curl -H 'Accept: application/json' https://hydra.nixos.org/jobset/nix/master/evals | \
#   jq -r '.evals[0].builds[] | @sh')
BUILDS_FOR_LATEST_EVAL=$(
curl -sS -H 'Accept: application/json' https://hydra.nixos.org/jobset/nix/master/evals | \
  jq -r '.evals[0].builds[]')

someBuildFailed=0

for buildId in $BUILDS_FOR_LATEST_EVAL; do
  buildInfo=$(curl -sS -H 'Accept: application/json' "https://hydra.nixos.org/build/$buildId")

  buildStatus=$(echo "$buildInfo" | \
    jq -r '.buildstatus')

  if [[ "$buildStatus" -ne 0 ]]; then
    someBuildFailed=1
    echo "Job “$(echo "$buildInfo" | jq -r '.job')” failed on hydra"
  fi
done

exit "$someBuildFailed"
