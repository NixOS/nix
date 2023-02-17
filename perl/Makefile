makefiles = local.mk

GLOBAL_CXXFLAGS += -g -Wall -std=c++2a -I../src -I../src/libutil/include

-include Makefile.config

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CXXFLAGS += -O3
else
  GLOBAL_CXXFLAGS += -O0
endif

include mk/lib.mk
