source common.sh

requireEnvironment
setupConfig
execUnshare ./delete-duplicate-inner.sh
