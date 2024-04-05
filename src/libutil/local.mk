libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc $(d)/signature/*.cc)
ifdef HOST_UNIX
  libutil_SOURCES += $(wildcard $(d)/unix/*.cc)
endif
ifdef HOST_LINUX
    libutil_SOURCES += $(wildcard $(d)/linux/*.cc)
endif

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libutil := -I $(d)
ifdef HOST_UNIX
  INCLUDE_libutil += -I $(d)/unix
endif
ifdef HOST_LINUX
  INCLUDE_libutil += -I $(d)/linux
endif
libutil_CXXFLAGS += $(INCLUDE_libutil)

libutil_LDFLAGS += $(THREAD_LDFLAGS) $(LIBCURL_LIBS) $(SODIUM_LIBS) $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) $(LIBARCHIVE_LIBS) $(BOOST_LDFLAGS) -lboost_context

$(foreach i, $(wildcard $(d)/args/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/args, 0644)))
$(foreach i, $(wildcard $(d)/signature/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/signature, 0644)))


ifeq ($(HAVE_LIBCPUID), 1)
  libutil_LDFLAGS += -lcpuid
endif
