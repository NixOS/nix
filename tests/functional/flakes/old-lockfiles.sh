#!/usr/bin/env bash

source ./common.sh

requireGit

repo="$TEST_ROOT/repo"

createGitRepo "$repo"

cat > "$repo/flake.nix" <<EOF
{
  inputs = {
    dependency.url = "git+file:///no-such-path?dir=subdir";
  };
  outputs = { dependency, self }: {
    hi = dependency.an_output;
  };
}
EOF

cat > "$repo/flake.lock" <<EOF
{
  "nodes": {
    "dependency": {
      "locked": {
        "dir": "subdir",
        "lastModified": 1746721011,
        "narHash": "sha256-9aIDvIdyHAfQyvT5SwPgYxUUhf1GwQVAWq+qa5LcEQE=",
        "ref": "refs/heads/master",
        "rev": "432058dbfc82b0369bc9cce440e4af2aece52b54",
        "revCount": 1,
        "type": "git",
        "url": "file:///no-such-path?dir=subdir"
      },
      "original": {
        "dir": "subdir",
        "type": "git",
        "url": "file:///no-such-path?dir=subdir"
      }
    },
    "root": {
      "inputs": {
        "dependency": "dependency"
      }
    }
  },
  "root": "root",
  "version": 7
}
EOF

git -C "$repo" add flake.nix flake.lock
git -C "$repo" commit -a -m foo

cp "$repo/flake.lock" "$repo/flake.lock.old"

nix flake lock "$repo"

cmp "$repo/flake.lock" "$repo/flake.lock.old"
