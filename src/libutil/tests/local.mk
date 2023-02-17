check: libutil-tests_RUN

programs += libutil-tests

libutil-tests-exe_NAME = libnixutil-tests

libutil-tests-exe_DIR := $(d)

libutil-tests-exe_INSTALL_DIR :=

libutil-tests-exe_LIBS = libutil-tests

libutil-tests-exe_LDFLAGS := $(GTEST_LIBS)

libraries += libutil-tests

libutil-tests_NAME = libnixutil-tests

libutil-tests_DIR := $(d)

libutil-tests_INSTALL_DIR :=

libutil-tests_SOURCES := $(wildcard $(d)/*.cc)

libutil-tests_CXXFLAGS += -I src/libutil/include

libutil-tests_LIBS = libutil

libutil-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
