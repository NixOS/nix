libraries += libutil-test-support

libutil-test-support_NAME = libnixutil-test-support

libutil-test-support_DIR := $(d)

libutil-test-support_INSTALL_DIR :=

libutil-test-support_SOURCES := $(wildcard $(d)/tests/*.cc)

libutil-test-support_CXXFLAGS += $(libutil-tests_EXTRA_INCLUDES)

libutil-test-support_LIBS = libutil

libutil-test-support_LDFLAGS := -lrapidcheck
