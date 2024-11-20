#!/usr/bin/env bash

source ./common.sh

TODO_NixOS

createFlake1

lockfileSummaryFlake=$TEST_ROOT/lockfileSummaryFlake
createGitRepo "$lockfileSummaryFlake" "--initial-branch=main"

# Test that the --commit-lock-file-summary flag and its alias work
cat > "$lockfileSummaryFlake/flake.nix" <<EOF
{
  inputs = {
    flake1.url = "git+file://$flake1Dir";
  };

  description = "lockfileSummaryFlake";

  outputs = inputs: rec {
    packages.$system.default = inputs.flake1.packages.$system.foo;
  };
}
EOF

git -C "$lockfileSummaryFlake" add flake.nix
git -C "$lockfileSummaryFlake" commit -m 'Add lockfileSummaryFlake'

testSummary="test summary 1"
nix flake lock "$lockfileSummaryFlake" --commit-lock-file --commit-lock-file-summary "$testSummary"
[[ -e "$lockfileSummaryFlake/flake.lock" ]]
[[ -z $(git -C "$lockfileSummaryFlake" diff main || echo failed) ]]
[[ "$(git -C "$lockfileSummaryFlake" log --format=%s -n 1)" = "$testSummary" ]]

git -C "$lockfileSummaryFlake" rm :/:flake.lock
git -C "$lockfileSummaryFlake" commit -m "remove flake.lock"
testSummary="test summary 2"
# NOTE(cole-h): We use `--option` here because Nix settings do not currently support flag-ifying the
# alias of a setting: https://github.com/NixOS/nix/issues/10989
nix flake lock "$lockfileSummaryFlake" --commit-lock-file --option commit-lockfile-summary "$testSummary"
[[ -e "$lockfileSummaryFlake/flake.lock" ]]
[[ -z $(git -C "$lockfileSummaryFlake" diff main || echo failed) ]]
[[ "$(git -C "$lockfileSummaryFlake" log --format=%s -n 1)" = "$testSummary" ]]
