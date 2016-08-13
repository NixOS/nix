nix_bin_scripts := \
  $(d)/nix-build \
  $(d)/nix-channel \
  $(d)/nix-copy-closure \

bin-scripts += $(nix_bin_scripts)

nix_noinst_scripts := \
  $(d)/build-remote.pl \
  $(d)/nix-http-export.cgi \
  $(d)/nix-profile.sh \
  $(d)/nix-reduce-build

ifeq ($(OS), Darwin)
  nix_noinst_scripts += $(d)/resolve-system-dependencies.rb
endif

noinst-scripts += $(nix_noinst_scripts)

profiledir = $(sysconfdir)/profile.d

$(eval $(call install-file-as, $(d)/nix-profile.sh, $(profiledir)/nix.sh, 0644))
$(eval $(call install-program-in, $(d)/build-remote.pl, $(libexecdir)/nix))
ifeq ($(OS), Darwin)
  $(eval $(call install-program-in, $(d)/resolve-system-dependencies.rb, $(libexecdir)/nix))
endif
$(eval $(call install-symlink, nix-build, $(bindir)/nix-shell))

clean-files += $(nix_bin_scripts) $(nix_noinst_scripts)
