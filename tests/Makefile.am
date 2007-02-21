TESTS_ENVIRONMENT = $(SHELL) -e

extra1 = $(shell pwd)/test-tmp/shared

simple.sh: simple.nix
dependencies.sh gc.sh nix-push.sh nix-pull.in logging.sh nix-build.sh install-package.sh check-refs.sh: dependencies.nix
locking.sh: locking.nix
parallel.sh: parallel.nix
build-hook.sh: build-hook.nix
substitutes.sh: substitutes.nix
substitutes2.sh: substitutes2.nix
fallback.sh: fallback.nix
gc-concurrent.sh: gc-concurrent.nix gc-concurrent2.nix
user-envs.sh: user-envs.nix
fixed.sh: fixed.nix
gc-runtime.sh: gc-runtime.nix
check-refs.sh: check-refs.nix
filter-source.sh: filter-source.nix

TESTS = init.sh hash.sh lang.sh add.sh simple.sh dependencies.sh \
  locking.sh parallel.sh build-hook.sh substitutes.sh substitutes2.sh \
  fallback.sh nix-push.sh gc.sh gc-concurrent.sh verify.sh nix-pull.sh \
  referrers.sh user-envs.sh logging.sh nix-build.sh misc.sh fixed.sh \
  gc-runtime.sh install-package.sh check-refs.sh filter-source.sh

XFAIL_TESTS =

include ../substitute.mk

$(TESTS): common.sh

EXTRA_DIST = $(TESTS) \
  simple.nix.in simple.builder.sh \
  hash-check.nix \
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
  fixed.nix.in fixed.builder1.sh fixed.builder2.sh \
  gc-runtime.nix.in \
  check-refs.nix.in \
  filter-source.nix.in \
  $(wildcard lang/*.nix) $(wildcard lang/*.exp) $(wildcard lang/*.exp.xml) $(wildcard lang/*.flags) \
  common.sh.in
