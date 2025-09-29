# shellcheck shell=bash
source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./verify-inner.sh
