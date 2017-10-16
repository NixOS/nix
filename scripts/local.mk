nix_noinst_scripts := \
  $(d)/nix-http-export.cgi \
  $(d)/nix-profile.sh \
  $(d)/nix-reduce-build

noinst-scripts += $(nix_noinst_scripts)

profiledir = $(sysconfdir)/profile.d

$(eval $(call install-file-as, $(d)/nix-profile.sh, $(profiledir)/nix.sh, 0644))
$(eval $(call install-file-as, $(d)/nix-profile-daemon.sh, $(profiledir)/nix-daemon.sh, 0644))

clean-files += $(nix_noinst_scripts)
