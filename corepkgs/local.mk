corepkgs_FILES = \
  fetchurl.nix

$(foreach file,$(corepkgs_FILES),$(eval $(call install-data-in,$(d)/$(file),$(datadir)/nix/corepkgs)))
