EXTRA_DIST = compat-include

libexec_PROGRAMS = bsdiff bspatch

bsdiff_SOURCES = bsdiff.c

bsdiff_LDADD = -lbz2

bspatch_SOURCES = bspatch.c

bspatch_LDADD = -lbz2

AM_CFLAGS = -O3 ${bsddiff_compat_include}
