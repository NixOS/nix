bin_SCRIPTS = nix-switch nix-collect-garbage \
 nix-pull nix-push

noinst_SCRIPTS = nix-profile.sh

install-exec-local:
	$(INSTALL) -d $(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(sysconfdir)/profile.d/nix.sh
	$(INSTALL) -d $(sysconfdir)/nix
# !!! don't overwrite local modifications
	$(INSTALL_DATA) prebuilts.conf $(sysconfdir)/nix/prebuilts.conf

%: %.in Makefile
	sed \
	 -e s^@prefix\@^$(prefix)^g \
	 -e s^@sysconfdir\@^$(sysconfdir)^g \
	 -e s^@localstatedir\@^$(localstatedir)^g \
	 < $< > $@ || rm $@
	chmod +x $@
