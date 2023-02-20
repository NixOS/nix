libraries += libfetchers

libfetchers_NAME = libnixfetchers

libfetchers_DIR := $(d)

libfetchers_SOURCES := $(wildcard $(d)/*.cc)

libfetchers_CXXFLAGS += \
	-Isrc/libfetchers/include \
	-Isrc/libutil/include \
  -Isrc/libstore/include

libfetchers_LDFLAGS += -pthread

libfetchers_LIBS = libutil libstore

# old include paths

$(foreach i, $(wildcard src/libfetchers/include/nix/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

# new include paths

$(foreach i, $(wildcard src/libfetchers/include/nix/fetchers/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/fetchers, 0644)))
