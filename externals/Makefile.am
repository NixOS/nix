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


all: build-bzip2

EXTRA_DIST = $(BZIP2).tar.gz

ext-clean:
	$(RM) -f have-bzip2 build-bzip2
	$(RM) -rf $(BZIP2)
