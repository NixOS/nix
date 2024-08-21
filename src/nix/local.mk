programs += nix

nix_DIR := $(d)

nix_SOURCES := \
  $(wildcard $(d)/*.cc) \
  $(wildcard src/nix-build/*.cc) \
  $(wildcard src/nix-env/*.cc) \
  $(wildcard src/nix-instantiate/*.cc) \
  $(wildcard src/nix-store/*.cc)

ifdef HOST_UNIX
nix_SOURCES += \
  $(wildcard $(d)/unix/*.cc) \
  $(wildcard src/build-remote/*.cc) \
  $(wildcard src/nix-channel/*.cc) \
  $(wildcard src/nix-collect-garbage/*.cc) \
  $(wildcard src/nix-copy-closure/*.cc) \
  $(wildcard src/nix-daemon/*.cc)
endif

INCLUDE_nix := -I $(d)
ifdef HOST_UNIX
  INCLUDE_nix += -I $(d)/unix
endif

nix_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libfetchers) $(INCLUDE_libexpr) $(INCLUDE_libflake) $(INCLUDE_libmain) -I src/libcmd -I doc/manual $(INCLUDE_nix)

nix_CXXFLAGS += -DNIX_BIN_DIR=\"$(NIX_ROOT)$(bindir)\"

nix_LIBS = libexpr libmain libfetchers libflake libstore libutil libcmd

nix_LDFLAGS = $(THREAD_LDFLAGS) $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) $(LOWDOWN_LIBS)

ifdef HOST_WINDOWS
  # Increase the default reserved stack size to 65 MB so Nix doesn't run out of space
  nix_LDFLAGS += -Wl,--stack,$(shell echo $$((65 * 1024 * 1024)))
endif

$(foreach name, \
  nix-build nix-channel nix-collect-garbage nix-copy-closure nix-daemon nix-env nix-hash nix-instantiate nix-prefetch-url nix-shell nix-store, \
  $(eval $(call install-symlink, nix, $(bindir)/$(name))))
$(eval $(call install-symlink, $(bindir)/nix, $(libexecdir)/nix/build-remote))

src/nix-env/user-env.cc: src/nix-env/buildenv.nix.gen.hh

$(d)/develop.cc: $(d)/get-env.sh.gen.hh

src/nix-channel/nix-channel.cc: src/nix-channel/unpack-channel.nix.gen.hh

$(d)/main.cc: \
  doc/manual/generate-manpage.nix.gen.hh \
  doc/manual/utils.nix.gen.hh doc/manual/generate-settings.nix.gen.hh \
  doc/manual/generate-store-info.nix.gen.hh \
  $(d)/help-stores.md.gen.hh

$(d)/profile.cc: $(d)/profile.md

$(d)/profile.md: $(d)/profiles.md.gen.hh
