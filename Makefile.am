SUBDIRS = externals src scripts corepkgs doc

EXTRA_DIST = substitute.mk nix.spec

rpm: dist
	rpm -ta $(distdir).tar.gz
