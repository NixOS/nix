# Berkeley DB

DB = db-4.0.14
DB_URL = http://www.sleepycat.com/update/snapshot/db-4.0.14.tar.gz

$(DB).tar.gz:
	wget $(DB_URL)

$(DB): $(DB).tar.gz
	gunzip < $(DB).tar.gz | tar xvf -

build-db: $(DB)
	(pfx=`pwd` && \
	cd $(DB)/build_unix && \
	CC=$(CC) CXX=$(CXX) ../dist/configure --prefix=$$pfx/inst --enable-cxx --disable-shared && \
	make && \
	make install)
	touch build-db


# CWI ATerm

ATERM = aterm-2.0
ATERM_URL = http://www.cwi.nl/projects/MetaEnv/aterm/aterm-2.0.tar.gz

$(ATERM).tar.gz:
	wget $(ATERM_URL)

$(ATERM): $(ATERM).tar.gz
	gunzip < $(ATERM).tar.gz | tar xvf -

build-aterm: $(ATERM)
	(pfx=`pwd` && \
	cd $(ATERM) && \
	./configure --prefix=$$pfx/inst && \
	make && \
	make install)
	touch build-aterm


all: build-db build-aterm

EXTRA_DIST = $(DB).tar.gz $(ATERM).tar.gz
