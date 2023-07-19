dyn-drv-tests := \
  $(d)/text-hashed-output.sh \
  $(d)/recursive-mod-json.sh

install-tests-groups += dyn-drv

clean-files += \
  $(d)/config.nix

test-deps += \
  tests/dyn-drv/config.nix
