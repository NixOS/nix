noinst_LIBRARIES = libmain.a

libmain_a_SOURCES = shared.cc shared.hh

AM_CXXFLAGS = \
 -DNIX_STORE_DIR=\"$(storedir)\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 -DNIX_VERSION=\"$(VERSION)\" \
 -I.. ${aterm_include} -I../libutil -I../libstore
