libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

libutil_LDFLAGS += -pthread $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) $(LIBARCHIVE_LIBS) $(BOOST_LDFLAGS) -lboost_context

ifeq ($(HAVE_LIBCPUID), 1)
	libutil_LDFLAGS += -lcpuid
endif
