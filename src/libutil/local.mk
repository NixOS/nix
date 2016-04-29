libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

libutil_LDFLAGS = -llzma -lbz2 -pthread $(OPENSSL_LIBS)

libutil_LIBS = libformat
