libraries += libplugintest

libplugintest_DIR := $(d)

libplugintest_SOURCES := $(d)/plugintest.cc

libplugintest_ALLOW_UNDEFINED := 1

libplugintest_EXCLUDE_FROM_LIBRARY_LIST := 1

libplugintest_CXXFLAGS := -I src/libutil -I src/libexpr -I src/libfetchers -I src/libstore
