ifeq ($(HAVE_LIBCPUID), 1)
	nix_tests += compute-levels.sh
endif

tests-environment = NIX_REMOTE= $(bash) -e

clean-files += $(d)/common.sh $(d)/config.nix $(d)/ca/config.nix

test-deps += tests/common.sh tests/config.nix tests/ca/config.nix

ifeq ($(BUILD_SHARED_LIBS), 1)
  test-deps += tests/plugins/libplugintest.$(SO_EXT)
endif

bats.test: $(test-deps)
	$(eval J := $(patsubst -j%,%,$(filter -j%,$(MAKEFLAGS))))
	cd tests && bats $(if $(J),--jobs $(J),) bats_tests/*

installcheck: bats.test $(installcheck)
