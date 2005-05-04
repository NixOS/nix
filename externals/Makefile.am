# Berkeley DB

DB = db-4.2.52

$(DB).tar.gz:
	@echo "Nix requires Berkeley DB to build."
	@echo "Please download version 4.2.52 from"
	@echo "  http://www.sleepycat.com/update/snapshot/db-4.2.52.tar.gz"
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

ATERM = aterm-2.3.1

$(ATERM).tar.gz:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version 2.3.1 from"
	@echo "  http://www.cwi.nl/projects/MetaEnv/aterm/aterm-2.3.1.tar.gz"
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
	CC="$(CC)" ./configure --prefix=$$pfx/inst-aterm && \
	$(MAKE) && \
	$(MAKE) install)
	touch build-aterm
endif


all: build-db build-aterm

EXTRA_DIST = $(DB).tar.gz $(ATERM).tar.gz

ext-clean:
	$(RM) -f have-db build-db have-aterm build-aterm
	$(RM) -rf $(DB) $(ATERM)
