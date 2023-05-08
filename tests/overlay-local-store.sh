source common.sh

requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"
if [[ $(uname) != Linux ]]; then skipTest "Need Linux for overlayfs"; fi

exec unshare --mount --map-root-user overlay-local-store/inner.sh
