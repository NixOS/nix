SUBDIRS = externals src scripts corepkgs doc
EXTRA_DIST = substitute.mk nix.spec nix.spec.in

include ./substitute.mk

nix.spec: nix.spec.in

rpm: nix.spec dist
	rpm $(EXTRA_RPM_FLAGS) -ta $(distdir).tar.gz

relname:
	echo -n $(distdir) > relname
