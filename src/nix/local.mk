programs += nix mini-nix

define common-exe
  $(1)_SOURCES = \
    $$(wildcard $(d)/common/*.cc) \
    $$(wildcard src/build-remote/*.cc) \
    $$(wildcard src/nix-daemon/*.cc) \
    $$(wildcard src/nix-store/*.cc)

  $(1)_CXXFLAGS += -I src/libutil -I src/libstore -I src/libmain -I src/libstore-cmd -I doc/manual

  $(1)_LIBS = libmain libstore libutil libstore-cmd

  $(1)_LDFLAGS = -pthread $$(SODIUM_LIBS) $$(EDITLINE_LIBS) $$(BOOST_LDFLAGS) $$(LOWDOWN_LIBS)
endef

$(eval $(call common-exe,mini-nix))

mini-nix_DIR := $(d)/store

mini-nix_SOURCES += \
  $(wildcard $(mini-nix_DIR)/*.cc)

ifeq ($(NIX_FULL), 1)

$(eval $(call common-exe,nix))

nix_DIR := $(d)/full

nix_SOURCES += \
  $(wildcard $(nix_DIR)/*.cc) \
  $(wildcard src/nix-build/*.cc) \
  $(wildcard src/nix-channel/*.cc) \
  $(wildcard src/nix-collect-garbage/*.cc) \
  $(wildcard src/nix-copy-closure/*.cc) \
  $(wildcard src/nix-env/*.cc) \
  $(wildcard src/nix-instantiate/*.cc)

nix_CXXFLAGS += -I src/libfetchers -I src/libexpr -I src/libcmd

nix_LIBS += libexpr libfetchers libcmd

$(foreach name, \
  nix-build nix-channel nix-collect-garbage nix-copy-closure nix-env nix-hash nix-instantiate nix-prefetch-url nix-shell, \
  $(eval $(call install-symlink, nix, $(bindir)/$(name))))

src/nix-env/user-env.cc: src/nix-env/buildenv.nix.gen.hh

src/nix/full/develop.cc: src/nix/full/get-env.sh.gen.hh

src/nix-channel/nix-channel.cc: src/nix-channel/unpack-channel.nix.gen.hh

src/nix/full/main.cc: \
  doc/manual/generate-manpage.nix.gen.hh \
  doc/manual/utils.nix.gen.hh doc/manual/generate-settings.nix.gen.hh \
  doc/manual/generate-store-info.nix.gen.hh \
  src/nix/generated-doc/help-stores.md

src/nix/full/generated-doc/files/%.md: doc/manual/src/command-ref/files/%.md
	@mkdir -p $$(dirname $@)
	@cp $< $@

src/nix/full/profile.cc: src/nix/full/profile.md src/nix/full/generated-doc/files/profiles.md.gen.hh

src/nix/generated-doc/help-stores.md: doc/manual/src/store/types/index.md.in
	@mkdir -p $$(dirname $@)
	@echo 'R"(' >> $@.tmp
	@echo >> $@.tmp
	@cat $^ >> $@.tmp
	@echo >> $@.tmp
	@echo ')"' >> $@.tmp
	@mv $@.tmp $@

endif

$(foreach name, \
  nix-daemon nix-store, \
  $(eval $(call install-symlink, nix, $(bindir)/$(name))))
$(eval $(call install-symlink, $(bindir)/nix, $(libexecdir)/nix/build-remote))
