# CWI ATerm

ATERM = aterm-$(ATERM_VERSION)

$(ATERM).tar.gz:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version $(ATERM_VERSION) from"
	@echo "  http://nixos.org/tarballs/aterm-$(ATERM_VERSION).tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(ATERM): $(ATERM).tar.gz
	gzip -d < $(srcdir)/$(ATERM).tar.gz | tar xvf -
	patch -d $(ATERM) -p1 < ./max-long.patch
	patch -d $(ATERM) -p1 < ./sizeof.patch

if HAVE_ATERM
build-aterm:
else
build-aterm: $(ATERM)
	(cd $(ATERM) && \
	CC="$(CC)" ./configure --prefix=$(pkglibdir)/dummy --libdir=${pkglibdir} && \
	$(MAKE) && \
	$(MAKE) check)
	touch build-aterm

install-exec-local::
	cd $(ATERM) && make install
	rm -rf "$(DESTDIR)/$(pkglibdir)/dummy"
endif


# bzip2

BZIP2 = bzip2-1.0.5

$(BZIP2).tar.gz:
	@echo "Nix requires bzip2 to build."
	@echo "Please download version 1.0.5 from"
	@echo "  http://www.bzip.org/1.0.5/bzip2-1.0.5.tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(BZIP2): $(BZIP2).tar.gz
	gunzip < $(srcdir)/$(BZIP2).tar.gz | tar xvf -

if HAVE_BZIP2
build-bzip2:
else
build-bzip2: $(BZIP2)
	(cd $(BZIP2) && \
	$(MAKE) && \
	$(MAKE) install PREFIX=$(abs_builddir)/inst-bzip2)
	touch build-bzip2

install-exec-local::
	mkdir -p $(DESTDIR)${bzip2_bin}
	$(INSTALL_PROGRAM) $(bzip2_bin_test)/bzip2 $(bzip2_bin_test)/bunzip2 $(DESTDIR)${bzip2_bin}
endif


# SQLite

SQLITE = sqlite-$(SQLITE_VERSION)
SQLITE_TAR = sqlite-amalgamation-$(SQLITE_VERSION).tar.gz

$(SQLITE_TAR):
	@echo "Nix requires the SQLite library to build."
	@echo "Please download version $(SQLITE_VERSION) from"
	@echo "  http://www.sqlite.org/$(SQLITE_TAR)"
	@echo "and place it in the externals/ directory."
	false

$(SQLITE): $(SQLITE_TAR)
	gzip -d < $(srcdir)/$(SQLITE_TAR) | tar xvf -

if HAVE_SQLITE
build-sqlite:
else
build-sqlite: $(SQLITE)
	(cd $(SQLITE) && \
	CC="$(CC)" ./configure --disable-static --prefix=$(pkglibdir)/dummy --libdir=${pkglibdir} && \
	$(MAKE) )
	touch build-sqlite

install-exec-local::
	cd $(SQLITE) && make install
	rm -rf "$(DESTDIR)/$(pkglibdir)/dummy"
endif


all: build-aterm build-bzip2 build-sqlite

EXTRA_DIST = $(ATERM).tar.gz $(BZIP2).tar.gz $(SQLITE_TAR) max-long.patch sizeof.patch

clean:
	$(RM) -f build-aterm build-bzip2 build-sqlite
	$(RM) -rf $(ATERM) $(BZIP2) $(SQLITE)
	$(RM) -rf inst-bzip2
