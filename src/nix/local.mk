programs += nix

nix_DIR := $(d)

nix_SOURCES := \
  $(wildcard $(d)/*.cc) \
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

nix_CXXFLAGS += -I src/libutil -I src/libstore -I src/libfetchers -I src/libexpr -I src/libmain

nix_LIBS = libexpr libmain libfetchers libstore libutil

nix_LDFLAGS = -pthread $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) -lboost_context -lboost_thread -lboost_system -llowdown

$(foreach name, \
  nix-build nix-channel nix-collect-garbage nix-copy-closure nix-daemon nix-env nix-hash nix-instantiate nix-prefetch-url nix-shell nix-store, \
  $(eval $(call install-symlink, nix, $(bindir)/$(name))))
$(eval $(call install-symlink, $(bindir)/nix, $(libexecdir)/nix/build-remote))

src/nix-env/user-env.cc: src/nix-env/buildenv.nix.gen.hh

src/nix/develop.cc: src/nix/get-env.sh.gen.hh
