programs += nix-build

nix-build_DIR := $(d)

nix-build_SOURCES := $(d)/nix-build.cc

nix-build_LIBS = libmain libexpr libstore libutil libformat

ifeq (MINGW,$(findstring MINGW,$(OS)))
#$(eval $(call install-symlink, nix-build.exe, $(bindir)/nix-shell.exe))
else
$(eval $(call install-symlink, nix-build, $(bindir)/nix-shell))
endif