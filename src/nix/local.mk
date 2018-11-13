programs += nix

nix_DIR := $(d)

nix_SOURCES := $(wildcard $(d)/*.cc) $(wildcard src/linenoise/*.cpp)

nix_LIBS = libexpr libmain libstore libutil libformat

nix_LDFLAGS = -pthread

ifeq (MINGW,$(findstring MINGW,$(OS)))
#$(eval $(call install-symlink, nix.exe, $(bindir)/nix-hash.exe))
else
$(eval $(call install-symlink, nix, $(bindir)/nix-hash))
endif
