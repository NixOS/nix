nix_bin_scripts := \
  $(d)/nix-copy-closure \

bin-scripts += $(nix_bin_scripts)

nix_noinst_scripts := \
  $(d)/build-remote.pl \
  $(d)/nix-http-export.cgi \
  $(d)/nix-profile.sh \
  $(d)/nix-reduce-build

noinst-scripts += $(nix_noinst_scripts)

profiledir = $(sysconfdir)/profile.d

$(eval $(call install-file-as, $(d)/nix-profile.sh, $(profiledir)/nix.sh, 0644))
$(eval $(call install-program-in, $(d)/build-remote.pl, $(libexecdir)/nix))

clean-files += $(nix_bin_scripts) $(nix_noinst_scripts)
