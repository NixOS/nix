SUBDIRS = bin2c boost libutil libstore libmain nix-store nix-hash \
 libexpr nix-instantiate nix-env nix-log2xml bsdiff-4.3

EXTRA_DIST = aterm-helper.pl

SETUID_PROGS = nix-store nix-instantiate nix-env
install-exec-hook:
if SETUID_HACK
if HAVE_SETRESUID
	cd $(DESTDIR)$(bindir) && chown @NIX_USER@ $(SETUID_PROGS) \
	  && chgrp @NIX_GROUP@ $(SETUID_PROGS) && chmod ug+s $(SETUID_PROGS)
else
	cd $(DESTDIR)$(bindir) && chown root $(SETUID_PROGS) && chmod u+s $(SETUID_PROGS)
endif
endif
