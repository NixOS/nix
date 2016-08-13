programs += resolve-system-dependencies

resolve-system-dependencies_DIR := $(d)

resolve-system-dependencies_LIBS := libstore libmain libutil libformat

resolve-system-dependencies_SOURCES := $(d)/resolve-system-dependencies.cc
