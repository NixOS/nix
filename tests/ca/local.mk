ca-tests := \
  $(d)/build-with-garbage-path.sh \
  $(d)/build.sh \
  $(d)/concurrent-builds.sh \
  $(d)/derivation-json.sh \
  $(d)/duplicate-realisation-in-closure.sh \
  $(d)/gc.sh \
  $(d)/import-derivation.sh \
  $(d)/new-build-cmd.sh \
  $(d)/nix-copy.sh \
  $(d)/nix-run.sh \
  $(d)/nix-shell.sh \
  $(d)/post-hook.sh \
  $(d)/recursive.sh \
  $(d)/repl.sh \
  $(d)/selfref-gc.sh \
  $(d)/signatures.sh \
  $(d)/substitute.sh \
  $(d)/why-depends.sh

install-tests-groups += ca

clean-files += \
  $(d)/config.nix

test-deps += \
  tests/ca/config.nix
