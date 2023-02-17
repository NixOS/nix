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
