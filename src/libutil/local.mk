libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc)

libutil_CXXFLAGS := -Isrc/libutil/include

libutil_LDFLAGS += -pthread $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) $(LIBARCHIVE_LIBS) $(BOOST_LDFLAGS) -lboost_context

ifeq ($(HAVE_LIBCPUID), 1)
	libutil_LDFLAGS += -lcpuid
endif

# old include paths

$(foreach i, $(wildcard src/libutil/include/nix/*.hh), \
	$(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

# new include paths

$(foreach i, $(wildcard src/libutil/include/nix/util/*.hh), \
	$(eval $(call install-file-in, $(i), $(includedir)/nix/util, 0644)))
