SUBDIRS = externals src scripts corepkgs doc

EXTRA_DIST = substitute.mk nix.spec

rpm: dist
	rpm $(EXTRA_RPM_FLAGS) -ta $(distdir).tar.gz
