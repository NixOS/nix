libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

libutil_CXXFLAGS += -I src/libutil

libutil_LDFLAGS += -pthread $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) $(LIBARCHIVE_LIBS) $(BOOST_LDFLAGS) -lboost_context

$(foreach i, $(wildcard $(d)/args/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/args, 0644)))

ifeq ($(HAVE_LIBCPUID), 1)
	libutil_LDFLAGS += -lcpuid
endif
