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
  buildInfo=$(curl --fail -sS -H 'Accept: application/json' "https://hydra.nixos.org/build/$buildId")

  finished=$(echo "$buildInfo" | jq -r '.finished')

  if [[ $finished = 0 ]]; then
    continue
  fi

  buildStatus=$(echo "$buildInfo" | jq -r '.buildstatus')

  if [[ $buildStatus != 0 ]]; then
    someBuildFailed=1
    echo "Job “$(echo "$buildInfo" | jq -r '.job')” failed on hydra: $buildInfo"
  fi
done

exit "$someBuildFailed"
