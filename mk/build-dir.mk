# Initialise support for build directories.
builddir ?=

ifdef builddir
  buildprefix = $(builddir)/
  buildprefixrel = $(builddir)
else
  buildprefix =
  buildprefixrel = .
endif
