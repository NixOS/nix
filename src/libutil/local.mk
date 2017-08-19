libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)
libutil_RELDIR := $(reldir)

libutil_SOURCES := $(subst $(d)/,,$(wildcard $(d)/*.cc))

libutil_LDFLAGS = $(LIBLZMA_LIBS) -lbz2 -pthread $(OPENSSL_LIBS)

libutil_LIBS = libformat

libutil_CXXFLAGS = -DBRO=\"$(bro)\"
