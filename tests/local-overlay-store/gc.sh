source common.sh

requireEnvironment
setupConfig
execUnshare ./gc-inner.sh
