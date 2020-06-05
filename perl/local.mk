nix_perl_sources := \
  lib/Nix/Store.pm \
  lib/Nix/Manifest.pm \
  lib/Nix/SSH.pm \
  lib/Nix/CopyClosure.pm \
  lib/Nix/Config.pm.in \
  lib/Nix/Utils.pm

nix_perl_modules := $(nix_perl_sources:.in=)

$(foreach x, $(nix_perl_modules), $(eval $(call install-data-in, $(x), $(perllibdir)/Nix)))

lib/Nix/Store.cc: lib/Nix/Store.xs
	$(trace-gen) xsubpp $^ -output $@

libraries += Store

Store_DIR := lib/Nix

Store_SOURCES := $(Store_DIR)/Store.cc

Store_CXXFLAGS = \
  $(NIX_CFLAGS) \
  -I$(shell perl -e 'use Config; print $$Config{archlibexp};')/CORE \
  -D_FILE_OFFSET_BITS=64 \
  -Wno-unknown-warning-option -Wno-unused-variable -Wno-literal-suffix \
  -Wno-reserved-user-defined-literal -Wno-duplicate-decl-specifier -Wno-pointer-bool-conversion

Store_LDFLAGS := $(SODIUM_LIBS) $(NIX_LIBS)

ifeq (CYGWIN,$(findstring CYGWIN,$(OS)))
  archlib = $(shell perl -E 'use Config; print $$Config{archlib};')
  libperl = $(shell perl -E 'use Config; print $$Config{libperl};')
  Store_LDFLAGS += $(shell find ${archlib} -name ${libperl})
endif

Store_ALLOW_UNDEFINED = 1

Store_FORCE_INSTALL = 1

Store_INSTALL_DIR = $(perllibdir)/auto/Nix/Store

clean-files += lib/Nix/Config.pm lib/Nix/Store.cc Makefile.config
