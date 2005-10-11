# Berkeley DB

DB = db-4.3.28.NC

$(DB).tar.gz:
	@echo "Nix requires Berkeley DB to build."
	@echo "Please download version 4.3.28 from"
	@echo "  http://downloads.sleepycat.com/db-4.3.28.NC.tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(DB): $(DB).tar.gz
	gunzip < $(DB).tar.gz | tar xvf -

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
	$(MAKE) install)
	touch build-db
endif


# CWI ATerm

ATERM = aterm-2.4.2

$(ATERM).tar.gz:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version 2.4.2 from"
	@echo "  http://www.cwi.nl/projects/MetaEnv/aterm/aterm-2.4.2.tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(ATERM): $(ATERM).tar.gz
	gunzip < $(ATERM).tar.gz | tar xvf -

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


all: build-db build-aterm

EXTRA_DIST = $(DB).tar.gz $(ATERM).tar.gz

ext-clean:
	$(RM) -f have-db build-db have-aterm build-aterm
	$(RM) -rf $(DB) $(ATERM)
