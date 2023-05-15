source common.sh

requireEnvironment
setupConfig
exec unshare --mount --map-root-user ./inner.sh
