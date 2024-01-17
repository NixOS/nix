dyn-drv-tests := \
  $(d)/text-hashed-output.sh \
  $(d)/recursive-mod-json.sh \
  $(d)/build-built-drv.sh \
  $(d)/eval-outputOf.sh \
  $(d)/dep-built-drv.sh \
  $(d)/old-daemon-error-hack.sh

install-tests-groups += dyn-drv

clean-files += \
  $(d)/config.nix

test-deps += \
  tests/functional/dyn-drv/config.nix
