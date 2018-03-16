libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

libutil_LDFLAGS = $(LIBLZMA_LIBS) -lbz2 -pthread $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) -lboost_context

libutil_LIBS = libformat

libutil_CXXFLAGS = -DBROTLI=\"$(brotli)\"
