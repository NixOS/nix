check: libutil-tests_RUN

programs += libutil-tests

libutil-tests_NAME = libnixutil-tests

libutil-tests_ENV := _NIX_TEST_UNIT_DATA=$(d)/data

libutil-tests_DIR := $(d)

libutil-tests_INSTALL_DIR :=

libutil-tests_SOURCES := $(wildcard $(d)/*.cc)

libutil-tests_EXTRA_INCLUDES = \
    -I tests/unit/libutil-support \
    -I src/libutil

libutil-tests_CXXFLAGS += $(libutil-tests_EXTRA_INCLUDES)

libutil-tests_LIBS = libutil-test-support libutil

libutil-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
