check: libexpr-tests_RUN

programs += libexpr-tests

libexpr-tests_DIR := $(d)

libexpr-tests_INSTALL_DIR :=

libexpr-tests_SOURCES := $(wildcard $(d)/*.cc)

libexpr-tests_CXXFLAGS += -I src/libexpr -I src/libutil -I src/libstore -I src/libexpr/tests -I src/libfetchers

libexpr-tests_LIBS = libexpr libutil libstore libfetchers

libexpr-tests_LDFLAGS := $(GTEST_LIBS) -lgmock
