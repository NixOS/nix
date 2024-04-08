source common.sh

requireEnvironment
setupConfig
execUnshare ./delete-refs-inner.sh
