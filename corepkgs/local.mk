corepkgs_FILES = \
  unpack-channel.nix \
  derivation.nix \
  fetchurl.nix \
  module.nix

$(foreach file,config.nix $(corepkgs_FILES),$(eval $(call install-data-in,$(d)/$(file),$(datadir)/nix/corepkgs)))

template-files += $(d)/config.nix
