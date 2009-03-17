TESTS_ENVIRONMENT = $(SHELL) -e

extra1 = $(shell pwd)/test-tmp/shared

TESTS = init.sh hash.sh lang.sh add.sh simple.sh dependencies.sh \
  locking.sh parallel.sh build-hook.sh substitutes.sh substitutes2.sh \
  fallback.sh nix-push.sh gc.sh gc-concurrent.sh verify.sh nix-pull.sh \
  referrers.sh user-envs.sh logging.sh nix-build.sh misc.sh fixed.sh \
  gc-runtime.sh install-package.sh check-refs.sh filter-source.sh \
  remote-store.sh export.sh export-graph.sh

XFAIL_TESTS =

include ../substitute.mk

$(TESTS): common.sh config.nix

EXTRA_DIST = $(TESTS) \
  simple.nix simple.builder.sh \
  hash-check.nix \
  dependencies.nix dependencies.builder*.sh \
  locking.nix locking.builder.sh \
  parallel.nix parallel.builder.sh \
  build-hook.nix build-hook.hook.sh \
  substituter.sh substituter2.sh \
  gc-concurrent.nix gc-concurrent.builder.sh gc-concurrent2.builder.sh \
  user-envs.nix user-envs.builder.sh \
  fixed.nix fixed.builder1.sh fixed.builder2.sh \
  gc-runtime.nix \
  check-refs.nix \
  filter-source.nix \
  export-graph.nix \
  $(wildcard lang/*.nix) $(wildcard lang/*.exp) $(wildcard lang/*.exp.xml) $(wildcard lang/*.flags) \
  common.sh.in
