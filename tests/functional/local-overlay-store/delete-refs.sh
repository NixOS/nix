# shellcheck shell=bash
source common.sh
source ../common/init.sh

requireEnvironment
setupConfig
execUnshare ./delete-refs-inner.sh
