libraries += libutil-test-support

libutil-test-support_NAME = libnixutil-test-support

libutil-test-support_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libutil-test-support_INSTALL_DIR := $(checklibdir)
else
  libutil-test-support_INSTALL_DIR :=
endif

libutil-test-support_SOURCES := $(wildcard $(d)/tests/*.cc)

libutil-test-support_CXXFLAGS += $(libutil-tests_EXTRA_INCLUDES)

# libexpr so we can steal their string printer from print.cc
libutil-test-support_LIBS = libutil libexpr

libutil-test-support_LDFLAGS := $(THREAD_LDFLAGS) -lrapidcheck
