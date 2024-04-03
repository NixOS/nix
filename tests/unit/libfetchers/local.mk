check: libfetchers-tests_RUN

programs += libfetchers-tests

libfetchers-tests_NAME = libnixfetchers-tests

libfetchers-tests_ENV := _NIX_TEST_UNIT_DATA=$(d)/data

libfetchers-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libfetchers-tests_INSTALL_DIR := $(checkbindir)
else
  libfetchers-tests_INSTALL_DIR :=
endif

libfetchers-tests_SOURCES := $(wildcard $(d)/*.cc)

libfetchers-tests_EXTRA_INCLUDES = \
    -I tests/unit/libstore-support \
    -I tests/unit/libutil-support \
    $(INCLUDE_libfetchers) \
    $(INCLUDE_libstore) \
    $(INCLUDE_libutil)

libfetchers-tests_CXXFLAGS += $(libfetchers-tests_EXTRA_INCLUDES)

libfetchers-tests_LIBS = \
    libstore-test-support libutil-test-support \
    libfetchers libstore libutil

libfetchers-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
