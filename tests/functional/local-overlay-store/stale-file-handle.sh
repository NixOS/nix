source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./stale-file-handle-inner.sh
