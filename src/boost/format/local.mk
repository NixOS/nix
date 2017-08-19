libraries += libformat

libformat_NAME = libnixformat

libformat_DIR := $(d)
libformat_RELDIR := $(reldir)

libformat_SOURCES := $(subst $(d)/,,$(wildcard $(d)/*.cc))
