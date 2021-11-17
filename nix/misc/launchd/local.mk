ifdef HOST_DARWIN

  $(eval $(call install-data-in, $(d)/org.nixos.nix-daemon.plist, $(prefix)/Library/LaunchDaemons))

endif
