check: libstore-tests_RUN

programs += libstore-tests

libstore-tests_NAME = libnixstore-tests

libstore-tests_ENV := _NIX_TEST_UNIT_DATA=$(d)/data

libstore-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libstore-tests_INSTALL_DIR := $(checkbindir)
else
  libstore-tests_INSTALL_DIR :=
endif

libstore-tests_SOURCES := $(wildcard $(d)/*.cc)

libstore-tests_EXTRA_INCLUDES = \
    -I tests/unit/libstore-support \
    -I tests/unit/libutil-support \
    $(INCLUDE_libstore) \
    $(INCLUDE_libstorec) \
    $(INCLUDE_libutil) \
    $(INCLUDE_libutilc)

libstore-tests_CXXFLAGS += $(libstore-tests_EXTRA_INCLUDES)

libstore-tests_LIBS = \
    libstore-test-support libutil-test-support \
    libstore libstorec libutil libutilc

libstore-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)
