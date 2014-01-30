LIBS += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

ifeq ($(HAVE_OPENSSL), 1)
  libutil_LDFLAGS = $(OPENSSL_LIBS)
else
  libutil_SOURCES += $(d)/md5.c $(d)/sha1.c $(d)/sha256.c
endif

libutil_LIBS = libformat
