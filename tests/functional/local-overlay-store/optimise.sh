source common.sh

requireEnvironment
setupConfig
execUnshare ./optimise-inner.sh
