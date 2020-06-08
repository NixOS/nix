check: libfetchers-tests_RUN

programs += libfetchers-tests

libfetchers-tests_DIR := $(d)

libfetchers-tests_INSTALL_DIR :=

libfetchers-tests_SOURCES := $(wildcard $(d)/*.cc)

libfetchers-tests_CXXFLAGS += -I src/libfetchers  -I src/libstore -I src/libutil

libfetchers-tests_LIBS = libfetchers libutil libstore libnixrust

libfetchers-tests_LDFLAGS := $(GTEST_LIBS)
