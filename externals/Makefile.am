# CWI ATerm

ATERM = aterm-2.4.2-fixes-r2

$(ATERM).tar.bz2:
	@echo "Nix requires the CWI ATerm library to build."
	@echo "Please download version 2.4.2-fixes-r2 from"
	@echo "  http://nixos.org/tarballs/aterm-2.4.2-fixes-r2.tar.bz2"
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


all: build-aterm build-bzip2

EXTRA_DIST = $(ATERM).tar.bz2 $(BZIP2).tar.gz

ext-clean:
	$(RM) -f have-aterm build-aterm have-bzip2 build-bzip2
	$(RM) -rf $(ATERM) $(BZIP2)
