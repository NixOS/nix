source common.sh

requireEnvironment
setupConfig
execUnshare ./delete-inner.sh
