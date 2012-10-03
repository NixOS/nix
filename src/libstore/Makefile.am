pkglib_LTLIBRARIES = libstore.la

libstore_la_SOURCES = \
  store-api.cc local-store.cc remote-store.cc derivations.cc build.cc misc.cc \
  globals.cc references.cc pathlocks.cc gc.cc \
  optimise-store.cc

pkginclude_HEADERS = \
  store-api.hh local-store.hh remote-store.hh derivations.hh misc.hh \
  globals.hh references.hh pathlocks.hh \
  worker-protocol.hh

libstore_la_LIBADD = ../libutil/libutil.la ../boost/format/libformat.la @SQLITE3_LIBS@ -lbz2

EXTRA_DIST = schema.sql

AM_CXXFLAGS = -Wall \
 -I$(srcdir)/.. -I$(srcdir)/../libutil \
 -DNIX_STORE_DIR=\"$(storedir)\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 -DNIX_CONF_DIR=\"$(sysconfdir)/nix\" \
 -DNIX_LIBEXEC_DIR=\"$(libexecdir)\" \
 -DNIX_BIN_DIR=\"$(bindir)\" \
 -I$(srcdir)/.. -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore

local-store.lo: schema.sql.hh

%.sql.hh: %.sql
	sed -e 's/"/\\"/g' -e 's/.*/"\0\\n"/' < $< > $@ || (rm $@ && exit 1)
