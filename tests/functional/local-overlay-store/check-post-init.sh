source common.sh

requireEnvironment
setupConfig
execUnshare ./check-post-init-inner.sh
