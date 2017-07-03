libraries += libstore

libstore_NAME = libnixstore

libstore_DIR := $(d)
libstore_RELDIR := $(reldir)

libstore_SOURCES := $(wildcard $(d)/*.cc)

libstore_LIBS = libutil libformat

libstore_LDFLAGS = $(SQLITE3_LIBS) -lbz2 $(LIBCURL_LIBS) $(SODIUM_LIBS) -pthread

ifeq ($(ENABLE_S3), 1)
	libstore_LDFLAGS += -laws-cpp-sdk-s3 -laws-cpp-sdk-core
endif

ifeq ($(OS), SunOS)
	libstore_LDFLAGS += -lsocket
endif

ifeq ($(OS), Linux)
	libstore_LDFLAGS += -lseccomp
endif

libstore_CXXFLAGS = \
 -DNIX_PREFIX=\"$(prefix)\" \
 -DNIX_STORE_DIR=\"$(storedir)\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 -DNIX_CONF_DIR=\"$(sysconfdir)/nix\" \
 -DNIX_LIBEXEC_DIR=\"$(libexecdir)\" \
 -DNIX_BIN_DIR=\"$(bindir)\" \
 -DSANDBOX_SHELL="\"$(sandbox_shell)\"" \
 -DLSOF=\"$(lsof)\"

$(d)/local-store.cc: $(d)/schema.sql.gen.hh

sandbox-headers = $(d)/sandbox-defaults.sb.gen.hh $(d)/sandbox-network.sb.gen.hh $(d)/sandbox-minimal.sb.gen.hh

$(d)/build.cc: $(sandbox-headers)

%.gen.hh: %
	@echo 'R"foo(' >> $@.tmp
	$(trace-gen) cat $< >> $@.tmp
	@echo ')foo"' >> $@.tmp
	@mv $@.tmp $@

clean-files += $(d)/schema.sql.gen.hh $(sandbox-headers)

$(eval $(call install-file-in, $(d)/nix-store.pc, $(prefix)/lib/pkgconfig, 0644))
