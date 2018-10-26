programs += nix

nix_DIR := $(d)

nix_SOURCES := \
  $(wildcard $(d)/*.cc) \
  $(wildcard src/linenoise/*.cpp) \
  $(wildcard src/build-remote/*.cc) \
  $(wildcard src/nix-build/*.cc) \
  $(wildcard src/nix-channel/*.cc) \
  $(wildcard src/nix-collect-garbage/*.cc) \
  $(wildcard src/nix-copy-closure/*.cc) \
  $(wildcard src/nix-daemon/*.cc) \
  $(wildcard src/nix-env/*.cc) \
  $(wildcard src/nix-instantiate/*.cc) \
  $(wildcard src/nix-prefetch-url/*.cc) \
  $(wildcard src/nix-store/*.cc) \

nix_LIBS = libexpr libmain libstore libutil libformat

nix_LDFLAGS = -pthread $(SODIUM_LIBS)

$(foreach name, \
  nix-build nix-channel nix-collect-garbage nix-copy-closure nix-daemon nix-env nix-hash nix-instantiate nix-prefetch-rul nix-shell nix-shore, \
  $(eval $(call install-symlink, nix, $(bindir)/$(name))))
$(eval $(call install-symlink, $(bindir)/nix, $(libexecdir)/nix/build-remote))
