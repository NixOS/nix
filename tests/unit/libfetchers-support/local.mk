libraries += libfetchers-test-support

libfetchers-test-support_NAME = libnixfetchers-test-support

libfetchers-test-support_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libfetchers-test-support_INSTALL_DIR := $(checklibdir)
else
  libfetchers-test-support_INSTALL_DIR :=
endif

libfetchers-test-support_SOURCES := $(wildcard $(d)/tests/*.cc)

libfetchers-test-support_CXXFLAGS += $(libfetchers-tests_EXTRA_INCLUDES)

libfetchers-test-support_LIBS = libutil

libfetchers-test-support_LDFLAGS := $(THREAD_LDFLAGS) -lrapidcheck
