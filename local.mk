ifeq ($(MAKECMDGOALS), dist)
  # Make sure we are in repo root with `--git-dir`
  dist-files += $(shell git --git-dir=.git ls-files || find * -type f)
endif

dist-files += configure $(TOP)/config.h.in nix.spec perl/configure

clean-files += Makefile.config

GLOBAL_CXXFLAGS += -I $(TOP)/.
GLOBAL_CXXFLAGS += -I $(TOP)/src
GLOBAL_CXXFLAGS += -I $(TOP)/src/libutil
GLOBAL_CXXFLAGS += -I $(TOP)/src/libstore
GLOBAL_CXXFLAGS += -I $(TOP)/src/libmain
GLOBAL_CXXFLAGS += -I $(TOP)/src/libexpr

$(foreach i, config.h $(call rwildcard, src/lib*, *.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(foreach i, $(call rwildcard, src/boost, *.hpp), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/$(patsubst src/%/,%,$(dir $(i))), 0644)))
