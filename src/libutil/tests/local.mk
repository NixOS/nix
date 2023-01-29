check: libutil-tests_RUN

programs += libutil-tests

libutil-tests_NAME := libnixutil-tests

libutil-tests_DIR := $(d)

libutil-tests_INSTALL_DIR :=

libutil-tests_SOURCES := $(wildcard $(d)/*.cc)

libutil-tests_CXXFLAGS += -I src/libutil -I src/libexpr

libutil-tests_LIBS = libutil

libutil-tests_LDFLAGS := $(GTEST_LIBS)
