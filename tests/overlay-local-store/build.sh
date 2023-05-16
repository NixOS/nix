source common.sh

requireEnvironment
setupConfig
execUnshare ./build-inner.sh
