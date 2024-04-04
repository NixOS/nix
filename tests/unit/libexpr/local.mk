check: libexpr-tests_RUN

programs += libexpr-tests

libexpr-tests_NAME := libnixexpr-tests

libexpr-tests_ENV := _NIX_TEST_UNIT_DATA=$(d)/data

libexpr-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libexpr-tests_INSTALL_DIR := $(checkbindir)
else
  libexpr-tests_INSTALL_DIR :=
endif

libexpr-tests_SOURCES := \
    $(wildcard $(d)/*.cc) \
    $(wildcard $(d)/value/*.cc) \
    $(wildcard $(d)/flake/*.cc)

libexpr-tests_EXTRA_INCLUDES = \
    -I tests/unit/libexpr-support \
    -I tests/unit/libstore-support \
    -I tests/unit/libutil-support \
    $(INCLUDE_libexpr) \
    $(INCLUDE_libexprc) \
    $(INCLUDE_libfetchers) \
    $(INCLUDE_libstore) \
    $(INCLUDE_libstorec) \
    $(INCLUDE_libutil) \
    $(INCLUDE_libutilc)

libexpr-tests_CXXFLAGS += $(libexpr-tests_EXTRA_INCLUDES)

libexpr-tests_LIBS = \
    libexpr-test-support libstore-test-support libutils-test-support \
    libexpr libexprc libfetchers libstore libstorec libutil libutilc

libexpr-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS) -lgmock
