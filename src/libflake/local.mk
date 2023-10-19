libraries += libflake

libflake_NAME = libnixflake

libflake_DIR := $(d)

libflake_SOURCES := $(wildcard $(d)/*.cc)

libflake_CXXFLAGS += -I src/libutil -I src/libstore -I src/libfetchers

libflake_LDFLAGS += -pthread

libflake_LIBS = libutil libstore libfetchers
