libexec_PROGRAMS = bsdiff bspatch

bsdiff_SOURCES = bsdiff.c

bsdiff_LDADD = ${bzip2_lib}

bspatch_SOURCES = bspatch.c

bspatch_LDADD = ${bzip2_lib}

AM_CFLAGS = -O3 ${bzip2_include} ${bsddiff_compat_include}
