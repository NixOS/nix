AC_INIT(nix, 0.3)
AC_CONFIG_SRCDIR(src/nix.cc)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

AC_PATH_PROG(wget, wget)

AC_CHECK_LIB(pthread, pthread_mutex_init)

AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile 
   externals/Makefile
   boost/Makefile boost/format/Makefile
   src/Makefile
   scripts/Makefile
   corepkgs/Makefile corepkgs/fetchurl/Makefile corepkgs/nar/Makefile
   doc/Makefile doc/manual/Makefile
  ])
AC_OUTPUT
