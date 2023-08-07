libraries += libutilc

libutilc_NAME = libnixutilc

libutilc_DIR := $(d)

libutilc_SOURCES := $(wildcard $(d)/*.cc)

libutilc_CXXFLAGS += -I src/libutil

libutilc_LIBS = libutil

libutilc_LDFLAGS += -pthread

libutilc_FORCE_INSTALL := 1
