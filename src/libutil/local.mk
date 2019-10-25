libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

libutil_LDFLAGS = $(LIBLZMA_LIBS) -lbz2 -pthread $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) $(BOOST_LDFLAGS)

ifeq (MINGW,$(findstring MINGW,$(OS)))
libutil_LDFLAGS += -lbacktrace -lboost_context-mt
else
libutil_LDFLAGS += -lboost_context
endif
