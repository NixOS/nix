check: libfetchers-tests-exe_RUN

programs += libfetchers-tests-exe

libfetchers-tests-exe_NAME = libnixstore-tests

libfetchers-tests-exe_DIR := $(d)

libfetchers-tests-exe_INSTALL_DIR :=

libfetchers-tests-exe_LIBS = libfetchers-tests

libfetchers-tests-exe_LDFLAGS := $(GTEST_LIBS)

libraries += libfetchers-tests

libfetchers-tests_NAME = libnixfetchers-tests

libfetchers-tests_DIR := $(d)

libfetchers-tests_INSTALL_DIR :=

libfetchers-tests_SOURCES := $(wildcard $(d)/*.cc)

libfetchers-tests_CXXFLAGS += -I src/libfetchers -I src/libutil

libfetchers-tests_LIBS = libutil-tests libfetchers libutil

libfetchers-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
