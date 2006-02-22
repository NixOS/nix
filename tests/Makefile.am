TEST_ROOT = $(shell pwd)/test-tmp

extra1 = $(TEST_ROOT)/shared

TESTS_ENVIRONMENT = TEST_ROOT=$(TEST_ROOT) \
  NIX_STORE_DIR=$(TEST_ROOT)/store \
  NIX_DATA_DIR=$(TEST_ROOT)/data \
  NIX_LOCALSTATE_DIR=$(TEST_ROOT)/var \
  NIX_LOG_DIR=$(TEST_ROOT)/var/log/nix \
  NIX_STATE_DIR=$(TEST_ROOT)/var/nix \
  NIX_DB_DIR=$(TEST_ROOT)/db \
  NIX_CONF_DIR=$(TEST_ROOT)/etc \
  NIX_BIN_DIR=$(TEST_ROOT)/bin \
  NIX_LIBEXEC_DIR=$(TEST_ROOT)/bin \
  REAL_BIN_DIR=$(bindir) \
  REAL_LIBEXEC_DIR=$(libexecdir) \
  REAL_LOCALSTATE_DIR=$(localstatedir) \
  REAL_DATA_DIR=$(datadir) \
  REAL_STORE_DIR=$(storedir) \
  NIX_BUILD_HOOK= \
  PERL=$(perl) \
  TOP=$(shell pwd)/.. \
  SHARED=$(extra1) \
  aterm_bin=$(aterm_bin) \
  dot=$(dot) \
  $(SHELL) -e

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

langtests:
	$(TESTS_ENVIRONMENT) ./lang.sh

include ../substitute.mk

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
  $(wildcard lang/*.nix) $(wildcard lang/*.exp)
