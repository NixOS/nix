libraries += libfetchers

libfetchers_NAME = libnixfetchers

libfetchers_DIR := $(d)

libfetchers_SOURCES := $(wildcard $(d)/*.cc)

libfetchers_CXXFLAGS += -I src/libutil -I src/libstore

libfetchers_LDFLAGS += -pthread $(LIBGIT2_LIBS) -larchive

libfetchers_LIBS = libutil libstore

$(foreach i, $(wildcard $(d)/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))
