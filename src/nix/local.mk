programs += nix

nix_DIR := $(d)

nix_SOURCES := \
  $(wildcard $(d)/*.cc) \
  $(wildcard src/build-remote/*.cc) \
  $(wildcard src/nix-build/*.cc) \
  $(wildcard src/nix-channel/*.cc) \
  $(wildcard src/nix-collect-garbage/*.cc) \
  $(wildcard src/nix-copy-closure/*.cc) \
  $(wildcard src/nix-daemon/*.cc) \
  $(wildcard src/nix-env/*.cc) \
  $(wildcard src/nix-instantiate/*.cc) \
  $(wildcard src/nix-store/*.cc)

ifdef HOST_UNIX
nix_SOURCES += \
  $(wildcard $(d)/unix/*.cc)
endif

INCLUDE_nix := -I $(d)
ifdef HOST_UNIX
  INCLUDE_nix += -I $(d)/unix
endif

nix_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libfetchers) $(INCLUDE_libexpr) $(INCLUDE_libmain) -I src/libcmd -I doc/manual $(INCLUDE_nix)

nix_LIBS = libexpr libmain libfetchers libstore libutil libcmd

nix_LDFLAGS = $(THREAD_LDFLAGS) $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) $(LOWDOWN_LIBS)

$(foreach name, \
  nix-build nix-channel nix-collect-garbage nix-copy-closure nix-daemon nix-env nix-hash nix-instantiate nix-prefetch-url nix-shell nix-store, \
  $(eval $(call install-symlink, nix, $(bindir)/$(name))))
$(eval $(call install-symlink, $(bindir)/nix, $(libexecdir)/nix/build-remote))

src/nix-env/user-env.cc: src/nix-env/buildenv.nix.gen.hh

src/nix/develop.cc: src/nix/get-env.sh.gen.hh

src/nix-channel/nix-channel.cc: src/nix-channel/unpack-channel.nix.gen.hh

src/nix/main.cc: \
  doc/manual/generate-manpage.nix.gen.hh \
  doc/manual/utils.nix.gen.hh doc/manual/generate-settings.nix.gen.hh \
  doc/manual/generate-store-info.nix.gen.hh \
  src/nix/generated-doc/help-stores.md

src/nix/generated-doc/files/%.md: doc/manual/src/command-ref/files/%.md
	@mkdir -p $$(dirname $@)
	@cp $< $@

src/nix/profile.cc: src/nix/profile.md src/nix/generated-doc/files/profiles.md.gen.hh

src/nix/generated-doc/help-stores.md: doc/manual/src/store/types/index.md.in
	@mkdir -p $$(dirname $@)
	@echo 'R"(' >> $@.tmp
	@echo >> $@.tmp
	@cat $^ >> $@.tmp
	@echo >> $@.tmp
	@echo ')"' >> $@.tmp
	@mv $@.tmp $@
