# CWI ATerm

ATERM = aterm-2.5

$(ATERM).tar.gz:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version 2.5 from"
	@echo "  http://nixos.org/tarballs/aterm-2.5.tar.gz"
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
	(pfx=`pwd` && \
	cd $(ATERM) && \
	CC="$(CC)" ./configure --prefix=$$pfx/inst-aterm \
	  --disable-shared --enable-static && \
	$(MAKE) && \
	$(MAKE) check && \
	$(MAKE) install)
	touch build-aterm
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
	(pfx=`pwd` && \
	cd $(BZIP2) && \
	$(MAKE) && \
	$(MAKE) install PREFIX=$$pfx/inst-bzip2)
	touch build-bzip2

install:
	mkdir -p $(DESTDIR)${bzip2_bin}
	$(INSTALL_PROGRAM) $(bzip2_bin_test)/bzip2 $(bzip2_bin_test)/bunzip2 $(DESTDIR)${bzip2_bin}
endif


# SQLite

SQLITE_VERSION = 3.6.22
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
	(pfx=`pwd` && \
	cd $(SQLITE) && \
	CC="$(CC)" ./configure --prefix=$$pfx/inst-sqlite \
	  --disable-shared --enable-static && \
	$(MAKE) && \
	$(MAKE) check && \
	$(MAKE) install)
	touch build-sqlite
endif


all: build-aterm build-bzip2 build-sqlite

EXTRA_DIST = $(ATERM).tar.gz $(BZIP2).tar.gz $(SQLITE_TAR) max-long.patch sizeof.patch

clean:
	$(RM) -f build-aterm build-bzip2 build-sqlite
	$(RM) -rf $(ATERM) $(BZIP2) $(SQLITE)
	$(RM) -rf inst-aterm inst-bzip2 inst-sqlite
