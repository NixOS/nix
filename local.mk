ifeq ($(MAKECMDGOALS), dist)
  dist-files += $(shell cat .dist-files)
endif

dist-files += configure config.h.in perl/configure

clean-files += Makefile.config

GLOBAL_CXXFLAGS += -I . -I src -I src/libutil -I src/libstore -I src/libmain -I src/libexpr -I src/nix -Wno-deprecated-declarations

$(foreach i, config.h $(call rwildcard, src/lib*, *.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix, 0644)))

$(GCH) $(PCH): src/libutil/util.hh config.h
