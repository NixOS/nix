nix_perl_sources := \
  $(d)/lib/Nix/Store.pm \
  $(d)/lib/Nix/Manifest.pm \
  $(d)/lib/Nix/GeneratePatches.pm \
  $(d)/lib/Nix/SSH.pm \
  $(d)/lib/Nix/CopyClosure.pm \
  $(d)/lib/Nix/Config.pm.in \
  $(d)/lib/Nix/Utils.pm \
  $(d)/lib/Nix/Crypto.pm

nix_perl_modules := $(nix_perl_sources:.in=)

$(foreach x, $(nix_perl_modules), $(eval $(call install-data-in, $(x), $(perllibdir)/Nix)))

ifeq ($(perlbindings), yes)

  $(d)/lib/Nix/Store.cc: $(d)/lib/Nix/Store.xs
	$(trace-gen) xsubpp $^ -output $@

  libraries += Store

  Store_DIR := $(d)/lib/Nix

  Store_SOURCES := $(Store_DIR)/Store.cc

  Store_LIBS = libstore

  Store_CXXFLAGS = \
    -I$(shell $(perl) -e 'use Config; print $$Config{archlibexp};')/CORE \
    -D_FILE_OFFSET_BITS=64 -Wno-unused-variable -Wno-literal-suffix

  Store_ALLOW_UNDEFINED = 1

  Store_FORCE_INSTALL = 1

  Store_INSTALL_DIR = $(perllibdir)/auto/Nix/Store

endif

clean-files += $(d)/lib/Nix/Config.pm $(d)/lib/Nix/Store.cc
