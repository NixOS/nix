libraries += libutilc

libutilc_NAME = libnixutilc

libutilc_DIR := $(d)

libutilc_SOURCES := $(wildcard $(d)/*.cc)

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libutilc := -I $(d)
libutilc_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libutilc)

libutilc_LIBS = libutil

libutilc_LDFLAGS += $(THREAD_LDFLAGS)

libutilc_FORCE_INSTALL := 1
