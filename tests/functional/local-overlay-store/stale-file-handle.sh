source common.sh

requireEnvironment
setupConfig
execUnshare ./stale-file-handle-inner.sh
