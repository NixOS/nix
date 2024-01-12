libraries += libstore-test-support

libstore-test-support_NAME = libnixstore-test-support

libstore-test-support_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libstore-test-support_INSTALL_DIR := $(checklibdir)
else
  libstore-test-support_INSTALL_DIR :=
endif

libstore-test-support_SOURCES := $(wildcard $(d)/tests/*.cc)

libstore-test-support_CXXFLAGS += $(libstore-tests_EXTRA_INCLUDES)

libstore-test-support_LIBS = \
    libutil-test-support \
    libstore libutil

libstore-test-support_LDFLAGS := $(THREAD_LDFLAGS) -lrapidcheck
