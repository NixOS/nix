libraries += libflake

libflake_NAME = libnixflake

libflake_DIR := $(d)

libflake_SOURCES := $(wildcard $(d)/*.cc $(d)/flake/*.cc)

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libflake := -I $(d)

libflake_CXXFLAGS += $(INCLUDE_libutil) $(INCLUDE_libstore) $(INCLUDE_libfetchers) $(INCLUDE_libexpr) $(INCLUDE_libflake)

libflake_LDFLAGS += $(THREAD_LDFLAGS)

libflake_LIBS = libutil libstore libfetchers libexpr
