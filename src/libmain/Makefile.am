noinst_LIBRARIES = libmain.a

libmain_a_SOURCES = shared.cc

AM_CXXFLAGS = \
 -DNIX_STORE_DIR=\"$(prefix)/store\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 -I.. -I../../externals/inst/include -I../libnix

EXTRA_DIST = *.hh
