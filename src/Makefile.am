bin_PROGRAMS = nix

nix_SOURCES = nix.cc md5.c
nix_CXXFLAGS = -DSYSTEM=\"@host@\"
nix_LDADD = -ldb_cxx-4 -lATerm

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/descriptors
	$(INSTALL) -d $(localstatedir)/nix/sources
	$(INSTALL) -d $(prefix)/pkg
	$(bindir)/nix init
