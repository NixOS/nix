nix_bin_scripts := \
  $(d)/nix-build \
  $(d)/nix-channel \
  $(d)/nix-copy-closure \
  $(d)/nix-generate-patches \
  $(d)/nix-install-package \
  $(d)/nix-pull \
  $(d)/nix-push

bin-scripts += $(nix_bin_scripts)

nix_substituters := \
  $(d)/copy-from-other-stores.pl \
  $(d)/download-from-binary-cache.pl \
  $(d)/download-using-manifests.pl

nix_noinst_scripts := \
  $(d)/build-remote.pl \
  $(d)/find-runtime-roots.pl \
  $(d)/resolve-system-dependencies.pl \
  $(d)/nix-http-export.cgi \
  $(d)/nix-profile.sh \
  $(d)/nix-profile-daemon.sh \
  $(d)/nix-reduce-build \
  $(nix_substituters)

noinst-scripts += $(nix_noinst_scripts)

profiledir = $(sysconfdir)/profile.d

$(eval $(call install-file-as, $(d)/nix-profile.sh, $(profiledir)/nix.sh, 0644))
$(eval $(call install-file-as, $(d)/nix-profile-daemon.sh, $(profiledir)/nix-daemon.sh, 0644))
$(eval $(call install-program-in, $(d)/find-runtime-roots.pl, $(libexecdir)/nix))
$(eval $(call install-program-in, $(d)/build-remote.pl, $(libexecdir)/nix))
$(eval $(call install-program-in, $(d)/resolve-system-dependencies.pl, $(libexecdir)/nix))
$(foreach prog, $(nix_substituters), $(eval $(call install-program-in, $(prog), $(libexecdir)/nix/substituters)))
$(eval $(call install-symlink, nix-build, $(bindir)/nix-shell))

clean-files += $(nix_bin_scripts) $(nix_noinst_scripts)
