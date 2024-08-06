source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./optimise-inner.sh
