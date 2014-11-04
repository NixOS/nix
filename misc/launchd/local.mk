ifeq ($(OS), Darwin)

  $(eval $(call install-data-in, $(d)/org.nixos.nix-daemon.plist, $(prefix)/Library/LaunchDaemons))

endif
