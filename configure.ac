AC_INIT(nix, "0.4")
AC_CONFIG_SRCDIR(README)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

# Put the revision number in the version.
if REVISION=`svnversion $srcdir 2> /dev/null`; then
   if test "$REVISION" != "exported"; then
      VERSION="$VERSION-r$REVISION"
   fi
fi

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
   src/Makefile
   src/boost/Makefile
   src/boost/format/Makefile
   src/libnix/Makefile
   src/libmain/Makefile
   src/nix/Makefile
   src/nix-hash/Makefile
   src/fix/Makefile
   scripts/Makefile
   corepkgs/Makefile
   corepkgs/fetchurl/Makefile
   corepkgs/nar/Makefile
   doc/Makefile
   doc/manual/Makefile
  ])
AC_OUTPUT
