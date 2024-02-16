source common.sh

# 27ce722638 required some incompatible changes to the nix file, so skip this
# tests for the newer versions
requireDaemonOlderThan "2.4pre20210622"

clearStore

outPath=$(nix-build structured-attrs.nix --no-out-link)

[[ $(cat $outPath/foo) = bar ]]
