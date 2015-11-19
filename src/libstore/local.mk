libraries += libstore

libstore_NAME = libnixstore

libstore_DIR := $(d)

libstore_SOURCES := $(wildcard $(d)/*.cc)

libstore_LIBS = libutil libformat

libstore_LDFLAGS = -lsqlite3 -lbz2 -lcurl

ifeq ($(OS), SunOS)
	libstore_LDFLAGS += -lsocket
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
 -DBASH_PATH="\"$(bash)\""

$(d)/local-store.cc: $(d)/schema.sql.hh

%.sql.hh: %.sql
	$(trace-gen) sed -e 's/"/\\"/g' -e 's/\(.*\)/"\1\\n"/' < $< > $@ || (rm $@ && exit 1)

clean-files += $(d)/schema.sql.hh

$(eval $(call install-file-in, $(d)/nix-store.pc, $(prefix)/lib/pkgconfig, 0644))
