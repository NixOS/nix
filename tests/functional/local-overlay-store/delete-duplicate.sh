source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./delete-duplicate-inner.sh
