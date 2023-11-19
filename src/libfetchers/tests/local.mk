check: libfetchers-tests-exe_RUN

programs += libfetchers-tests-exe

libfetchers-tests-exe_NAME = libnixfetchers-tests

libfetchers-tests-exe_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libfetchers-tests-exe_INSTALL_DIR := $(checkbindir)
else
  libfetchers-tests-exe_INSTALL_DIR :=
endif


libfetchers-tests-exe_LIBS = libfetchers-tests libstore


libfetchers-tests-exe_LDFLAGS := $(GTEST_LIBS)

libraries += libfetchers-tests

libfetchers-tests_NAME = libnixfetchers-tests

libfetchers-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libfetchers-tests_INSTALL_DIR := $(checklibdir)
else
  libfetchers-tests_INSTALL_DIR :=
endif

libfetchers-tests_SOURCES := $(wildcard $(d)/*.cc)

libfetchers-tests_CXXFLAGS += -I src/libfetchers -I src/libutil -I src/libstore

libfetchers-tests_LIBS = libutil-tests libstore-tests libutil libstore libfetchers

libfetchers-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
