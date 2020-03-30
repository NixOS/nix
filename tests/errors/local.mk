programs += error-test

error-test_DIR := $(d)

error-test_SOURCES := \
  $(wildcard $(d)/*.cc) \

error-test_LIBS = libutil

error-test_LDFLAGS = -pthread $(SODIUM_LIBS) $(EDITLINE_LIBS) $(BOOST_LDFLAGS) -lboost_context -lboost_thread -lboost_system

# $(foreach name, \
#   nix-build nix-channel nix-collect-garbage nix-copy-closure nix-daemon nix-env nix-hash nix-instantiate nix-prefetch-url nix-shell nix-store, \
#   $(eval $(call install-symlink, nix, $(bindir)/$(name))))
# $(eval $(call install-symlink, $(bindir)/nix, $(libexecdir)/nix/build-remote))

# src/nix-env/user-env.cc: src/nix-env/buildenv.nix.gen.hh
