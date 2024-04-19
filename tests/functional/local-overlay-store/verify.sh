source common.sh

requireEnvironment
setupConfig
execUnshare ./verify-inner.sh
