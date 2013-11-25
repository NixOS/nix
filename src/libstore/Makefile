LIBS += libstore

libstore_NAME = libnixstore

libstore_DIR := $(d)

libstore_SOURCES = \
  store-api.cc local-store.cc remote-store.cc derivations.cc build.cc misc.cc \
  globals.cc references.cc pathlocks.cc gc.cc optimise-store.cc

libstore_LIBS = libutil libformat

libstore_LDFLAGS = -lsqlite3 -lbz2

libstore_CXXFLAGS = \
 -DNIX_STORE_DIR=\"$(storedir)\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 -DNIX_CONF_DIR=\"$(sysconfdir)/nix\" \
 -DNIX_LIBEXEC_DIR=\"$(libexecdir)\" \
 -DNIX_BIN_DIR=\"$(bindir)\" \
 -DPACKAGE_VERSION=\"$(PACKAGE_VERSION)\"
