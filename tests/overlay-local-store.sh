source common.sh

requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"
if [[ $(uname) != Linux ]]; then skipTest "Need Linux for overlayfs"; fi
needLocalStore "The test uses --store always so we would just be bypassing the daemon"

echo "drop-supplementary-groups = false" >> "$NIX_CONF_DIR"/nix.conf

exec unshare --mount --map-root-user overlay-local-store/inner.sh
