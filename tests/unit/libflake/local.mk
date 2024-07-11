check: libflake-tests_RUN

programs += libflake-tests

libflake-tests_NAME := libnixflake-tests

libflake-tests_ENV := _NIX_TEST_UNIT_DATA=$(d)/data GTEST_OUTPUT=xml:$$testresults/libflake-tests.xml

libflake-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libflake-tests_INSTALL_DIR := $(checkbindir)
else
  libflake-tests_INSTALL_DIR :=
endif

libflake-tests_SOURCES := \
    $(wildcard $(d)/*.cc) \
    $(wildcard $(d)/value/*.cc) \
    $(wildcard $(d)/flake/*.cc)

libflake-tests_EXTRA_INCLUDES = \
    -I tests/unit/libflake-support \
    -I tests/unit/libstore-support \
    -I tests/unit/libutil-support \
    $(INCLUDE_libflake) \
    $(INCLUDE_libexpr) \
    $(INCLUDE_libfetchers) \
    $(INCLUDE_libstore) \
    $(INCLUDE_libutil) \

libflake-tests_CXXFLAGS += $(libflake-tests_EXTRA_INCLUDES)

libflake-tests_LIBS = \
    libexpr-test-support libstore-test-support libutil-test-support \
    libflake libexpr libfetchers libstore libutil

libflake-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS) -lgmock

ifdef HOST_WINDOWS
  # Increase the default reserved stack size to 65 MB so Nix doesn't run out of space
  libflake-tests_LDFLAGS += -Wl,--stack,$(shell echo $$((65 * 1024 * 1024)))
endif
