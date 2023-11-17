check: libutil-tests-exe_RUN

programs += libutil-tests-exe

libutil-tests-exe_NAME = libnixutil-tests

libutil-tests-exe_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libutil-tests-exe_INSTALL_DIR := $(checkbindir)
else
  libutil-tests-exe_INSTALL_DIR :=
endif

libutil-tests-exe_LIBS = libutil-tests

libutil-tests-exe_LDFLAGS := $(GTEST_LIBS)

libraries += libutil-tests

libutil-tests_NAME = libnixutil-tests

libutil-tests_DIR := $(d)

ifeq ($(INSTALL_UNIT_TESTS), yes)
  libutil-tests_INSTALL_DIR := $(checklibdir)
else
  libutil-tests_INSTALL_DIR :=
endif

libutil-tests_SOURCES := $(wildcard $(d)/*.cc)

libutil-tests_CXXFLAGS += -I src/libutil

libutil-tests_LIBS = libutil

libutil-tests_LDFLAGS := -lrapidcheck $(GTEST_LIBS)

check: unit-test-data/libutil/git/check-data.sh.test

$(eval $(call run-test,unit-test-data/libutil/git/check-data.sh))
