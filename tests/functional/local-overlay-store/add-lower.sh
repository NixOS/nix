source common.sh

requireEnvironment
setupConfig
execUnshare ./add-lower-inner.sh
