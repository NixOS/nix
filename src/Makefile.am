SUBDIRS = bin2c boost libutil libstore libmain nix-store nix-hash \
 libexpr nix-instantiate nix-env log2xml

SETUID_PROGS = nix-store nix-instantiate nix-env
install-exec-hook:
if SETUID_HACK
	cd $(DESTDIR)$(bindir) && chown root $(SETUID_PROGS) && chmod u+s $(SETUID_PROGS)
endif
