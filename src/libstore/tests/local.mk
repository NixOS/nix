check: libstore-tests-exe_RUN

programs += libstore-tests-exe

libstore-tests-exe_NAME = libnixstore-tests

libstore-tests-exe_DIR := $(d)

libstore-tests-exe_INSTALL_DIR :=

libstore-tests-exe_LIBS = libstore-tests

libstore-tests-exe_LDFLAGS := $(GTEST_LIBS)

libraries += libstore-tests

libstore-tests_NAME = libnixstore-tests

libstore-tests_DIR := $(d)

libstore-tests_INSTALL_DIR :=

libstore-tests_SOURCES := $(wildcard $(d)/*.cc)

libstore-tests_CXXFLAGS += -I src/libstore -I src/libutil

libstore-tests_LIBS = libutil-tests libstore libutil

libstore-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
