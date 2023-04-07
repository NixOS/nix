libraries += libstore

libstore_NAME = libnixstore

libstore_DIR := $(d)

libstore_SOURCES := $(wildcard $(d)/*.cc $(d)/builtins/*.cc $(d)/build/*.cc)

libstore_LIBS = libutil

libstore_LDFLAGS += $(SQLITE3_LIBS) $(LIBCURL_LIBS) $(SODIUM_LIBS) -pthread
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

libstore_CXXFLAGS += \
 -I src/libutil -I src/libstore -I src/libstore/build \
 -DNIX_PREFIX=\"$(prefix)\" \
 -DNIX_STORE_DIR=\"$(storedir)\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 -DNIX_CONF_DIR=\"$(sysconfdir)/nix\" \
 -DNIX_BIN_DIR=\"$(bindir)\" \
 -DNIX_MAN_DIR=\"$(mandir)\" \
 -DLSOF=\"$(lsof)\"

ifeq ($(embedded_sandbox_shell),yes)
libstore_CXXFLAGS += -DSANDBOX_SHELL=\"__embedded_sandbox_shell__\"

$(d)/build/local-derivation-goal.cc: $(d)/embedded-sandbox-shell.gen.hh

$(d)/embedded-sandbox-shell.gen.hh: $(sandbox_shell)
	$(trace-gen) hexdump -v -e '1/1 "0x%x," "\n"' < $< > $@.tmp
	@mv $@.tmp $@
else
ifneq ($(sandbox_shell),)
libstore_CXXFLAGS += -DSANDBOX_SHELL="\"$(sandbox_shell)\""
endif
endif

$(d)/local-store.cc: $(d)/schema.sql.gen.hh $(d)/ca-specific-schema.sql.gen.hh

$(d)/build.cc:

%.gen.hh: %
	@echo 'R"foo(' >> $@.tmp
	$(trace-gen) cat $< >> $@.tmp
	@echo ')foo"' >> $@.tmp
	@mv $@.tmp $@

clean-files += $(d)/schema.sql.gen.hh $(d)/ca-specific-schema.sql.gen.hh

$(eval $(call install-file-in, $(d)/nix-store.pc, $(libdir)/pkgconfig, 0644))

$(foreach i, $(wildcard src/libstore/builtins/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/builtins, 0644)))

$(foreach i, $(wildcard src/libstore/build/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/build, 0644)))
