AC_INIT(nix, "0.5")
AC_CONFIG_SRCDIR(README)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

# Put the revision number in the version.
if REVISION=`test -d $srcdir/.svn && svnversion $srcdir 2> /dev/null`; then
   VERSION="$VERSION-r$REVISION"
elif REVISION=`cat $srcdir/svn-revision 2> /dev/null`; then
   VERSION="$VERSION-r$REVISION"
fi

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

AC_PATH_PROG(wget, wget)
AC_PATH_PROG(xmllint, xmllint)
AC_PATH_PROG(xsltproc, xsltproc)

AC_ARG_WITH(docbook-catalog, AC_HELP_STRING([--with-docbook-catalog=PATH],
  [path of the DocBook XML DTD]),
  docbookcatalog=$withval, docbookcatalog=/docbook-dtd-missing)
AC_SUBST(docbookcatalog)

AC_ARG_WITH(docbook-xsl, AC_HELP_STRING([--with-docbook-xsl=PATH],
  [path of the DocBook XSL stylesheets]),
  docbookxsl=$withval, docbookxsl=/docbook-xsl-missing)
AC_SUBST(docbookxsl)

AC_ARG_WITH(xml-flags, AC_HELP_STRING([--xml-flags=FLAGS],
  [extra flags to be passed to xmllint and xsltproc]),
  xmlflags=$withval, xmlflags=)
AC_SUBST(xmlflags)

AC_CHECK_LIB(pthread, pthread_mutex_init)

AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile
   externals/Makefile
   src/Makefile
   src/bin2c/Makefile
   src/boost/Makefile
   src/boost/format/Makefile
   src/libutil/Makefile
   src/libstore/Makefile
   src/libmain/Makefile
   src/nix-store/Makefile
   src/nix-hash/Makefile
   src/libexpr/Makefile
   src/nix-instantiate/Makefile
   src/nix-env/Makefile
   scripts/Makefile
   corepkgs/Makefile
   corepkgs/fetchurl/Makefile
   corepkgs/nar/Makefile
   corepkgs/buildenv/Makefile
   doc/Makefile
   doc/manual/Makefile
  ])
AC_OUTPUT
