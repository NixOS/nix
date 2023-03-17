source common.sh

exec unshare --mount --map-root-user overlay-local-store/inner.sh
