TESTS_ENVIRONMENT = $(SHELL) -e

extra1 = $(shell pwd)/test-tmp/shared

simple.sh: simple.nix
dependencies.sh: dependencies.nix
locking.sh: locking.nix
parallel.sh: parallel.nix
build-hook.sh: build-hook.nix
substitutes.sh: substitutes.nix
substitutes2.sh: substitutes2.nix
fallback.sh: fallback.nix
nix-push.sh: dependencies.nix
nix-pull.sh: dependencies.nix
gc.sh: dependencies.nix
gc-concurrent.sh: gc-concurrent.nix gc-concurrent2.nix
user-envs.sh: user-envs.nix

TESTS = init.sh hash.sh lang.sh simple.sh dependencies.sh locking.sh parallel.sh \
  build-hook.sh substitutes.sh substitutes2.sh fallback.sh nix-push.sh gc.sh \
  gc-concurrent.sh verify.sh nix-pull.sh referrers.sh user-envs.sh misc.sh

XFAIL_TESTS =

include ../substitute.mk

$(TESTS): common.sh

EXTRA_DIST = $(TESTS) \
  simple.nix.in simple.builder.sh \
  dependencies.nix.in dependencies.builder*.sh \
  locking.nix.in locking.builder.sh \
  parallel.nix.in parallel.builder.sh \
  build-hook.nix.in build-hook.hook.sh \
  substitutes.nix.in substituter.sh \
  substitutes2.nix.in substituter2.sh \
  gc-concurrent.nix.in gc-concurrent.builder.sh \
  gc-concurrent2.nix.in gc-concurrent2.builder.sh \
  fallback.nix.in \
  user-envs.nix.in user-envs.builder.sh \
  $(wildcard lang/*.nix) $(wildcard lang/*.exp) \
  common.sh.in
