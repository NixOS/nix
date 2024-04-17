check: libutil-tests_RUN

programs += libutil-tests

libutil-tests_NAME = libnixutil-tests

libutil-tests_ENV := _NIX_TEST_UNIT_DATA=$(d)/data

libutil-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libutil-tests_INSTALL_DIR := $(checkbindir)
else
  libutil-tests_INSTALL_DIR :=
endif

libutil-tests_SOURCES := $(wildcard $(d)/*.cc)

libutil-tests_EXTRA_INCLUDES = \
    -I tests/unit/libutil-support \
    $(INCLUDE_libutil) \
    $(INCLUDE_libutilc)

libutil-tests_CXXFLAGS += $(libutil-tests_EXTRA_INCLUDES)

libutil-tests_LIBS = libutil-test-support libutil libutilc

libutil-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)

check: $(d)/data/git/check-data.sh.test

$(eval $(call run-test,$(d)/data/git/check-data.sh))
