TEST_ROOT = $(shell pwd)/test-tmp

extra1 = $(TEST_ROOT)/shared

TESTS_ENVIRONMENT = TEST_ROOT=$(TEST_ROOT) \
  NIX_STORE_DIR=$(TEST_ROOT)/store \
  NIX_DATA_DIR=$(TEST_ROOT)/data \
  NIX_LOG_DIR=$(TEST_ROOT)/log \
  NIX_STATE_DIR=$(TEST_ROOT)/state \
  NIX_DB_DIR=$(TEST_ROOT)/db \
  TOP=$(shell pwd)/.. \
  SHARED=$(extra1) \
  $(SHELL) -e -x

simple.sh: simple.nix
dependencies.sh: dependencies.nix
locking.sh: locking.nix
parallel.sh: parallel.nix
build-hook.sh: build-hook.nix
substitutes.sh: substitutes.nix substituter.nix
substitutes2.sh: substitutes2.nix substituter.nix substituter2.nix
fallback.sh: fallback.nix

TESTS = init.sh simple.sh dependencies.sh locking.sh parallel.sh \
  build-hook.sh substitutes.sh substitutes2.sh fallback.sh verify.sh

XFAIL_TESTS =

include ../substitute.mk

EXTRA_DIST = $(TESTS) \
  simple.nix.in simple.builder.sh \
  dependencies.nix.in dependencies.builder*.sh \
  locking.nix.in locking.builder.sh \
  parallel.nix.in parallel.builder.sh \
  build-hook.nix.in build-hook.hook.sh \
  substitutes.nix.in substituter.nix.in substituter.builder.sh \
  substitutes2.nix.in substituter2.nix.in substituter2.builder.sh \
  fallback.nix.in
