# Berkeley DB

DB = db-4.1.25

$(DB).tar.gz:
	@echo "Nix requires Berkeley DB to build."
	@echo "Please download version 4.1.25 from"
	@echo "  http://www.sleepycat.com/update/snapshot/db-4.1.25.tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(DB): $(DB).tar.gz
	gunzip < $(DB).tar.gz | tar xvf -

have-db:
	$(MAKE) $(DB)
	touch have-db

build-db: have-db
	(pfx=`pwd` && \
	cd $(DB)/build_unix && \
	CC=$(CC) CXX=$(CXX) ../dist/configure --prefix=$$pfx/inst \
	 --enable-cxx --disable-shared && \
	make && \
	make install)
	touch build-db


# CWI ATerm

ATERM = aterm-2.0.5

$(ATERM).tar.gz:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version 2.0.5 from"
	@echo "  http://www.cwi.nl/projects/MetaEnv/aterm/aterm-2.0.5.tar.gz"
	@echo "and place it in the externals/ directory."
	false

$(ATERM): $(ATERM).tar.gz
	gunzip < $(ATERM).tar.gz | tar xvf -

have-aterm:
	$(MAKE) $(ATERM)
	touch have-aterm

build-aterm: have-aterm
	(pfx=`pwd` && \
	cd $(ATERM) && \
	./configure --prefix=$$pfx/inst && \
	make && \
	make install)
	touch build-aterm


# SDF bundle

SDF2 = sdf2-bundle-1.6

$(SDF2).tar.gz:
	@echo "Nix requires the SDF2 bundle to build."
	@echo "Please download version 1.6 from"
	@echo "  ftp://ftp.stratego-language.org/pub/stratego/sdf2/sdf2-bundle-1.6.tar.gzP"
	@echo "and place it in the externals/ directory."
	false

$(SDF2): $(SDF2).tar.gz
	gunzip < $(SDF2).tar.gz | tar xvf -

have-sdf2:
	$(MAKE) $(SDF2)
	touch have-sdf2

build-sdf2: have-sdf2
	(pfx=`pwd` && \
	cd $(SDF2) && \
	./configure --prefix=$$pfx/inst && \
	make && \
	make install)
	touch build-sdf2


all: build-db build-aterm build-sdf2

EXTRA_DIST = $(DB).tar.gz $(ATERM).tar.gz $(SDF2).tar.gz
