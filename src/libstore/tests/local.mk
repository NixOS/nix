check: libstore-tests_RUN

programs += libstore-tests

libstore-tests_DIR := $(d)

libstore-tests_INSTALL_DIR :=

libstore-tests_SOURCES := $(wildcard $(d)/*.cc)

libstore-tests_CXXFLAGS += -I src/libstore -I src/libutil

libstore-tests_LIBS = libstore libutil

libstore-tests_LDFLAGS := $(GTEST_LIBS)
