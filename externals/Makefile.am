# Berkeley DB

DB = db-4.5.20

if OLD_DB_COMPAT

$(DB).tar.gz:
	@echo "Nix requires Berkeley DB to build."
	@echo "Please download version 4.5.20 from"
	@echo "  http://download-east.oracle.com/berkeley-db/db-4.5.20.tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(DB): $(DB).tar.gz
	gunzip < $(srcdir)/$(DB).tar.gz | tar xvf -
	(cd $(DB) && $(patch) -p1) < $(srcdir)/bdb-cygwin.patch

have-db:
	$(MAKE) $(DB)
	touch have-db

if HAVE_BDB
build-db:
else
build-db: have-db
	(pfx=`pwd` && \
	cd $(DB)/build_unix && \
	CC="$(CC)" CXX="$(CXX)" CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" \
	 ../dist/configure --prefix=$$pfx/inst-bdb \
	 --enable-cxx --disable-shared --disable-cryptography \
         --disable-replication --disable-verify && \
	$(MAKE) && \
	$(MAKE) install_include install_lib)
	touch build-db
endif

else

build-db:

endif


# CWI ATerm

ATERM = aterm-2.4.2-fixes-r2

$(ATERM).tar.bz2:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version 2.4.2-fixes-r2 from"
	@echo "  http://losser.st-lab.cs.uu.nl/~eelco/dist/aterm-2.4.2-fixes-r2.tar.bz2"
	@echo "and place it in the externals/ directory."
	false

$(ATERM): $(ATERM).tar.bz2
	bunzip2 < $(srcdir)/$(ATERM).tar.bz2 | tar xvf -

have-aterm:
	$(MAKE) $(ATERM)
	touch have-aterm

if HAVE_ATERM
build-aterm:
else
build-aterm: have-aterm
	(pfx=`pwd` && \
	cd $(ATERM) && \
	CC="$(CC)" ./configure --prefix=$$pfx/inst-aterm \
	  --disable-shared --enable-static && \
	$(MAKE) && \
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

have-bzip2:
	$(MAKE) $(BZIP2)
	touch have-bzip2

if HAVE_BZIP2
build-bzip2:
else
build-bzip2: have-bzip2
	(pfx=`pwd` && \
	cd $(BZIP2) && \
	$(MAKE) && \
	$(MAKE) install PREFIX=$$pfx/inst-bzip2)
	touch build-bzip2

install:
	mkdir -p $(DESTDIR)${bzip2_bin}
	$(INSTALL_PROGRAM) $(bzip2_bin_test)/bzip2 $(bzip2_bin_test)/bunzip2 $(DESTDIR)${bzip2_bin}
endif


all: build-db build-aterm build-bzip2

EXTRA_DIST = $(DB).tar.gz $(ATERM).tar.bz2 $(BZIP2).tar.gz \
  bdb-cygwin.patch

ext-clean:
	$(RM) -f have-db build-db have-aterm build-aterm have-bzip2 build-bzip2
	$(RM) -rf $(DB) $(ATERM) $(BZIP2)
