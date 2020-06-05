libraries += libstore

libstore_NAME = libnixstore

libstore_DIR := $(d)

libstore_SOURCES := \
	$(d)/binary-cache-store.cc \
	$(d)/build.cc \
	$(d)/builtins.cc \
	$(d)/crypto.cc \
	$(d)/derivations.cc \
	$(d)/download.cc \
	$(d)/export-import.cc \
	$(d)/gc.cc \
	$(d)/globals.cc \
	$(d)/http-binary-cache-store.cc \
	$(d)/local-binary-cache-store.cc \
	$(d)/local-fs-store.cc \
	$(d)/local-store.cc \
	$(d)/misc.cc \
	$(d)/nar-accessor.cc \
	$(d)/nar-info.cc \
	$(d)/nar-info-disk-cache.cc \
	$(d)/optimise-store.cc \
	$(d)/pathlocks.cc \
	$(d)/profiles.cc \
	$(d)/references.cc \
	$(d)/remote-fs-accessor.cc \
	$(d)/remote-store.cc \
	$(d)/sqlite.cc \
	$(d)/ssh-store.cc \
	$(d)/store-api.cc \
	$(d)/ipfs-binary-cache-store.cc

libstore_LIBS = libutil libformat

libstore_LDFLAGS = $(SQLITE3_LIBS) -lbz2 $(LIBCURL_LIBS) $(SODIUM_LIBS) -pthread

ifeq ($(ENABLE_S3), 1)
	libstore_LDFLAGS += -laws-cpp-sdk-s3 -laws-cpp-sdk-core
	libstore_SOURCES += $(d)/s3-binary-cache-store.cc
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
