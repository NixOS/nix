libraries += libfetchers

libfetchers_NAME = libnixfetchers

libfetchers_DIR := $(d)

libfetchers_SOURCES := $(wildcard $(d)/*.cc)
ifdef HOST_UNIX
  libfetchers_SOURCES += $(wildcard $(d)/unix/*.cc)
endif

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libfetchers := -I $(d)
ifdef HOST_UNIX
  INCLUDE_libfetchers += -I $(d)/unix
endif

libfetchers_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libfetchers)

libfetchers_LDFLAGS += $(THREAD_LDFLAGS) $(LIBGIT2_LIBS) -larchive

libfetchers_LIBS = libutil libstore
