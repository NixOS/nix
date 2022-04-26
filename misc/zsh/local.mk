$(eval $(call install-file-as, $(d)/completion.zsh, $(datarootdir)/zsh/site-functions/_nix, 0644))
$(eval $(call install-file-as, $(d)/run-help-nix, $(datarootdir)/zsh/site-functions/run-help-nix, 0644))
