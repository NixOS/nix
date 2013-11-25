LIBS += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES = util.cc hash.cc serialise.cc archive.cc xml-writer.cc affinity.cc

ifeq ($(HAVE_OPENSSL), 1)
  libutil_LDFLAGS = $(OPENSSL_LIBS)
else
  libutil_SOURCES += md5.c sha1.c sha256.c
endif

libutil_LIBS = libformat
