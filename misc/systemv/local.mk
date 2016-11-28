ifeq ($(initsystem), systemv)

  $(eval $(call install-data-in, $(d)/nix-daemon, $(prefix)/etc/init.d))

endif
