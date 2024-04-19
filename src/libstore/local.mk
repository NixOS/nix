libraries += libstore

libstore_NAME = libnixstore

libstore_DIR := $(d)

libstore_SOURCES := $(wildcard $(d)/*.cc $(d)/builtins/*.cc)
ifdef HOST_UNIX
  libstore_SOURCES += $(wildcard $(d)/unix/*.cc $(d)/unix/builtins/*.cc $(d)/unix/build/*.cc)
endif
ifdef HOST_LINUX
  libstore_SOURCES += $(wildcard $(d)/linux/*.cc)
endif
ifdef HOST_WINDOWS
  libstore_SOURCES += $(wildcard $(d)/windows/*.cc)
endif

libstore_LIBS = libutil

libstore_LDFLAGS += $(SQLITE3_LIBS) $(LIBCURL_LIBS) $(THREAD_LDFLAGS)
ifdef HOST_LINUX
  libstore_LDFLAGS += -ldl
endif

$(foreach file,$(libstore_FILES),$(eval $(call install-data-in,$(d)/$(file),$(datadir)/nix/sandbox)))

ifeq ($(ENABLE_S3), 1)
  libstore_LDFLAGS += -laws-cpp-sdk-transfer -laws-cpp-sdk-s3 -laws-cpp-sdk-core -laws-crt-cpp
endif

ifdef HOST_SOLARIS
  libstore_LDFLAGS += -lsocket
endif

ifeq ($(HAVE_SECCOMP), 1)
  libstore_LDFLAGS += $(LIBSECCOMP_LIBS)
endif

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libstore := -I $(d) -I $(d)/build
ifdef HOST_UNIX
  INCLUDE_libstore += -I $(d)/unix
endif
ifdef HOST_LINUX
  INCLUDE_libstore += -I $(d)/linux
endif
ifdef HOST_WINDOWS
  INCLUDE_libstore += -I $(d)/windows
endif

ifdef HOST_WINDOWS
NIX_ROOT = N:\\\\
else
NIX_ROOT =
endif

# Prefix all but `NIX_STORE_DIR`, since we aren't doing a local store
# yet so a "logical" store dir that is the same as unix is prefered.
#
# Also, it keeps the unit tests working.

libstore_CXXFLAGS += \
 $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libstore) \
 -DNIX_PREFIX=\"$(NIX_ROOT)$(prefix)\" \
 -DNIX_STORE_DIR=\"$(storedir)\" \
 -DNIX_DATA_DIR=\"$(NIX_ROOT)$(datadir)\" \
 -DNIX_STATE_DIR=\"$(NIX_ROOT)$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(NIX_ROOT)$(localstatedir)/log/nix\" \
 -DNIX_CONF_DIR=\"$(NIX_ROOT)$(sysconfdir)/nix\" \
 -DNIX_BIN_DIR=\"$(NIX_ROOT)$(bindir)\" \
 -DNIX_MAN_DIR=\"$(NIX_ROOT)$(mandir)\" \
 -DLSOF=\"$(NIX_ROOT)$(lsof)\"

ifeq ($(embedded_sandbox_shell),yes)
libstore_CXXFLAGS += -DSANDBOX_SHELL=\"__embedded_sandbox_shell__\"

$(d)/unix/build/local-derivation-goal.cc: $(d)/unix/embedded-sandbox-shell.gen.hh

$(d)/unix/embedded-sandbox-shell.gen.hh: $(sandbox_shell)
	$(trace-gen) hexdump -v -e '1/1 "0x%x," "\n"' < $< > $@.tmp
	@mv $@.tmp $@
else
  ifneq ($(sandbox_shell),)
    libstore_CXXFLAGS += -DSANDBOX_SHELL="\"$(sandbox_shell)\""
  endif
endif

$(d)/unix/local-store.cc: $(d)/unix/schema.sql.gen.hh $(d)/unix/ca-specific-schema.sql.gen.hh

$(d)/unix/build.cc:

clean-files += $(d)/unix/schema.sql.gen.hh $(d)/unix/ca-specific-schema.sql.gen.hh

$(eval $(call install-file-in, $(buildprefix)$(d)/nix-store.pc, $(libdir)/pkgconfig, 0644))

$(foreach i, $(wildcard src/libstore/builtins/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/builtins, 0644)))

$(foreach i, $(wildcard src/libstore/build/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/build, 0644)))
