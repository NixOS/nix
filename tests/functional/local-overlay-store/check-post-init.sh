# shellcheck shell=bash
source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./check-post-init-inner.sh
