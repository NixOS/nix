# Berkeley DB

DB = db-4.0.14

$(DB).tar.gz:
	echo "Nix requires Berkeley DB to build."
	echo "Please download version 4.0.14 from"
	echo "  http://www.sleepycat.com/update/snapshot/db-4.0.14.tar.gz"
	echo "and place it in the externals/ directory."
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

ATERM = aterm-2.0

$(ATERM).tar.gz:
	echo "Nix requires the CWI ATerm library to build."
	echo "Please download version 2.0 from"
	echo "  http://www.cwi.nl/projects/MetaEnv/aterm/aterm-2.0.tar.gz"
	echo "and place it in the externals/ directory."
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


all: build-db build-aterm

EXTRA_DIST = $(DB).tar.gz $(ATERM).tar.gz
