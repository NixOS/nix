AC_INIT(nix, 0.2pre1)
AC_CONFIG_SRCDIR(src/nix.cc)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

AC_CHECK_LIB(pthread, pthread_mutex_init)

AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile externals/Makefile src/Makefile scripts/Makefile
			  corepkgs/Makefile corepkgs/fetchurl/Makefile
			  corepkgs/nar/Makefile])
AC_OUTPUT
