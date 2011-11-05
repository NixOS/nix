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
	$(MAKE) CC="$(CC)" && \
	$(MAKE) install PREFIX=$(abs_builddir)/inst-bzip2)
	touch build-bzip2

install-exec-local:: build-bzip2
	mkdir -p $(DESTDIR)${bzip2_bin}
	$(INSTALL_PROGRAM) $(bzip2_bin_test)/bzip2 $(DESTDIR)${bzip2_bin}
endif


# SQLite

SQLITE = sqlite-autoconf-$(SQLITE_VERSION)
SQLITE_TAR = sqlite-autoconf-$(SQLITE_VERSION).tar.gz

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
	CC="$(CC)" CFLAGS="-DSQLITE_ENABLE_COLUMN_METADATA=1" ./configure --disable-static --prefix=$(pkglibdir)/dummy --libdir=${pkglibdir} $(SUB_CONFIGURE_FLAGS) && \
	$(MAKE) )
	touch build-sqlite

install-exec-local:: build-sqlite
	cd $(SQLITE) && $(MAKE) install
	rm -rf "$(DESTDIR)/$(pkglibdir)/dummy"
endif


all: build-bzip2 build-sqlite

EXTRA_DIST = $(BZIP2).tar.gz $(SQLITE_TAR)

clean:
	$(RM) -f build-bzip2 build-sqlite
	$(RM) -rf $(BZIP2) $(SQLITE)
	$(RM) -rf inst-bzip2
