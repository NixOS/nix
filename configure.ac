AC_INIT(nix, "0.5")
AC_CONFIG_SRCDIR(README)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

# Put the revision number in the version.
if REVISION=`test -d $srcdir/.svn && svnversion $srcdir 2> /dev/null`; then
   VERSION="${VERSION}pre${REVISION}"
elif REVISION=`cat $srcdir/svn-revision 2> /dev/null`; then
   VERSION="${VERSION}pre${REVISION}"
fi

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

# Construct a Nix system name (like "i686-linux").
AC_MSG_CHECKING([for the canonical Nix system name])
machine_name=`uname -m`
sys_name=`uname -s | tr [A-Z] [a-z]`
system="${machine_name}-${sys_name}"
AC_MSG_RESULT($system)
AC_SUBST(system)

AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

# Check for pubsetbuf.
AC_MSG_CHECKING([for pubsetbuf])
AC_LANG_PUSH(C++)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <iostream>
using namespace std;
static char buf[1024];]],
    [[cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));]])],
    [AC_MSG_RESULT(yes) AC_DEFINE(HAVE_PUBSETBUF, 1, [whether pubsetbuf is available])],
    AC_MSG_RESULT(no))
AC_LANG_POP(C++)

# Check for <locale>
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([locale])
AC_LANG_POP(C++)

AC_DEFUN([NEED_PROG],
[AC_PATH_PROG($1, $2)
if test -z "$$1"; then
    AC_MSG_ERROR([$1 is required])
fi
])

NEED_PROG(curl, curl)
NEED_PROG(bzip2, bzip2)
NEED_PROG(bunzip2, bunzip2)
NEED_PROG(shell, sh)
AC_PATH_PROG(xmllint, xmllint, false)
AC_PATH_PROG(xsltproc, xsltproc, false)
AC_PATH_PROG(flex, flex, false)
AC_PATH_PROG(bison, bison, false)
NEED_PROG(perl, perl)

AC_ARG_WITH(docbook-catalog, AC_HELP_STRING([--with-docbook-catalog=PATH],
  [path of the DocBook XML DTD]),
  docbookcatalog=$withval, docbookcatalog=/docbook-dtd-missing)
AC_SUBST(docbookcatalog)

AC_ARG_WITH(docbook-xsl, AC_HELP_STRING([--with-docbook-xsl=PATH],
  [path of the DocBook XSL stylesheets]),
  docbookxsl=$withval, docbookxsl=/docbook-xsl-missing)
AC_SUBST(docbookxsl)

AC_ARG_WITH(xml-flags, AC_HELP_STRING([--with-xml-flags=FLAGS],
  [extra flags to be passed to xmllint and xsltproc]),
  xmlflags=$withval, xmlflags=)
AC_SUBST(xmlflags)

AC_ARG_WITH(store-dir, AC_HELP_STRING([--with-store-dir=PATH],
  [path of the Nix store]),
  storedir=$withval, storedir='${prefix}/store')
AC_SUBST(storedir)

AC_ARG_WITH(bdb, AC_HELP_STRING([--with-bdb=PATH],
  [prefix of Berkeley DB]),
  bdb=$withval, bdb=)
AM_CONDITIONAL(HAVE_BDB, test -n "$bdb")
if test -z "$bdb"; then
  bdb_lib='-L${top_builddir}/externals/inst-bdb/lib -ldb_cxx'
  bdb_include='-I${top_builddir}/externals/inst-bdb/include'
else
  bdb_lib="-L$bdb/lib -Wl,-rpath,$bdb/lib -ldb_cxx"
  bdb_include="-I$bdb/include"
fi
AC_SUBST(bdb_lib)
AC_SUBST(bdb_include)

AC_ARG_WITH(aterm, AC_HELP_STRING([--with-aterm=PATH],
  [prefix of CWI ATerm library]),
  aterm=$withval, aterm=)
AM_CONDITIONAL(HAVE_ATERM, test -n "$aterm")
if test -z "$aterm"; then
  aterm_lib='-L${top_builddir}/externals/inst-aterm/lib -lATerm'
  aterm_include='-I${top_builddir}/externals/inst-aterm/include'
else
  aterm_lib="-L$aterm/lib -Wl,-rpath,$aterm/lib -lATerm"
  aterm_include="-I$aterm/include"
fi
AC_SUBST(aterm_lib)
AC_SUBST(aterm_include)

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
   src/log2xml/Makefile
   scripts/Makefile
   corepkgs/Makefile
   corepkgs/fetchurl/Makefile
   corepkgs/nar/Makefile
   corepkgs/buildenv/Makefile
   doc/Makefile
   doc/manual/Makefile
  ])
AC_OUTPUT
