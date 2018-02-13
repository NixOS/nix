libraries += plugintest

plugintest_DIR := $(d)

plugintest_SOURCES := $(d)/plugintest.cc

plugintest_ALLOW_UNDEFINED := 1

plugintest_EXCLUDE_FROM_LIBRARY_LIST := 1
