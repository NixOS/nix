source common.sh

requireEnvironment
setupConfig
execUnshare ./redundant-add-inner.sh
