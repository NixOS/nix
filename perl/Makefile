makefiles = local.mk

GLOBAL_CXXFLAGS += -g -Wall -std=c++2a

# A convenience for concurrent development of Nix and its Perl bindings.
# Not needed in a standalone build of the Perl bindings.
ifneq ("$(wildcard ../src)", "")
  GLOBAL_CXXFLAGS += -I ../src
endif

-include Makefile.config

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CXXFLAGS += -O3
else
  GLOBAL_CXXFLAGS += -O0
endif

include mk/lib.mk
