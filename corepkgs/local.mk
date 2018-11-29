corepkgs_FILES = \
  buildenv.nix \
  unpack-channel.nix \
  derivation.nix \
  fetchurl.nix \
  imported-drv-to-derivation.nix \
  default-installation-source.nix

$(foreach file,config.nix $(corepkgs_FILES),$(eval $(call install-data-in,$(d)/$(file),$(datadir)/nix/corepkgs)))

template-files += $(d)/config.nix
