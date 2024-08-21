source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./gc-inner.sh
