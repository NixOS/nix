source common.sh

touch foo -t 202211111111
# We only check whether 2222-11-1* **:**:** is the last modified date since
# `lastModified` is transformed into UTC in `builtins.fetchTarball`.
[[ "$(nix eval --impure --raw --expr "(builtins.fetchTree \"path://$PWD/foo\").lastModifiedDate")" =~ 2022111.* ]]
