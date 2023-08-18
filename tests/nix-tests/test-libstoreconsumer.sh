source common.sh

drv="$(nix-instantiate simple.nix)"
cat "$drv"
out="$(./test-libstoreconsumer/test-libstoreconsumer "$drv")"
cat "$out/hello" | grep -F "Hello World!"
