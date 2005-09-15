AC_INIT(nix, "0.9")
AC_CONFIG_SRCDIR(README)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE([dist-bzip2 foreign])
        
# Change to `1' to produce a `stable' release (i.e., the `preREVISION'
# suffix is not added).
STABLE=0

# Put the revision number in the version.
if test "$STABLE" != "1"; then
    if REVISION=`test -d $srcdir/.svn && svnversion $srcdir 2> /dev/null`; then
        VERSION="${VERSION}pre${REVISION}"
    elif REVISION=`cat $srcdir/svn-revision 2> /dev/null`; then
        VERSION="${VERSION}pre${REVISION}"
    fi
fi

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

# Construct a Nix system name (like "i686-linux").
AC_MSG_CHECKING([for the canonical Nix system name])
cpu_name=$(uname -p | tr 'A-Z ' 'a-z_')
machine_name=$(uname -m | tr 'A-Z ' 'a-z_')

case $machine_name in
    i*86)
        machine_name=i686
        ;;
    *)
        if test "$cpu_name" != "unknown"; then
            machine_name=$cpu_name
        fi
        ;;
esac

sys_name=$(uname -s | tr 'A-Z ' 'a-z_')
AC_ARG_WITH(system, AC_HELP_STRING([--with-system=SYSTEM],
  [platform identifier (e.g., `i686-linux')]),
  system=$withval, system="${machine_name}-${sys_name}")
AC_MSG_RESULT($system)
AC_SUBST(system)
AC_DEFINE_UNQUOTED(SYSTEM, ["$system"], [platform identifier (`cpu-os')])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

# We are going to use libtool.
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

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
[
AC_PATH_PROG($1, $2)
if test -z "$$1"; then
    AC_MSG_ERROR([$2 is required])
fi
])

NEED_PROG(curl, curl)
NEED_PROG(bzip2, bzip2)
NEED_PROG(bunzip2, bunzip2)
NEED_PROG(shell, sh)
AC_PATH_PROG(xmllint, xmllint, false)
AC_PATH_PROG(xsltproc, xsltproc, false)
AC_PATH_PROG(w3m, w3m, false)
AC_PATH_PROG(flex, flex, false)
AC_PATH_PROG(bison, bison, false)
NEED_PROG(perl, perl)
NEED_PROG(tar, tar)
                
NEED_PROG(cat, cat)
AC_ARG_WITH(coreutils-bin, AC_HELP_STRING([--with-coreutils-bin=PATH],
  [path of cat, mkdir, etc.]),
  coreutils=$withval, coreutils=$(dirname $cat))
AC_SUBST(coreutils)

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
  bdb_lib="-L$bdb/lib -ldb_cxx"
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
  aterm_bin='${top_builddir}/externals/inst-aterm/bin'
else
  aterm_lib="-L$aterm/lib -lATerm"
  aterm_include="-I$aterm/include"
  aterm_bin="$aterm/bin"
fi
AC_SUBST(aterm_lib)
AC_SUBST(aterm_include)
AC_SUBST(aterm_bin)

AC_CHECK_LIB(pthread, pthread_mutex_init)

AC_ARG_ENABLE(init-state, AC_HELP_STRING([--disable-init-state],
  [do not initialise DB etc. in `make install']),
  init_state=$enableval, init_state=yes)
AM_CONDITIONAL(INIT_STATE, test "$init_state" = "yes")


AC_ARG_ENABLE(setuid, AC_HELP_STRING([--enable-setuid],
  [install Nix setuid]),
  setuid_hack=$enableval, setuid_hack=no)
AM_CONDITIONAL(SETUID_HACK, test "$setuid_hack" = "yes")
if test "$setuid_hack" = "yes"; then
    AC_DEFINE(SETUID_HACK, 1, [whether to install Nix setuid])
fi

AC_CHECK_FUNC(setresuid, [HAVE_SETRESUID=1], [HAVE_SETRESUID=])
AM_CONDITIONAL(HAVE_SETRESUID, test "$HAVE_SETRESUID" = "1")
if test "$HAVE_SETRESUID" = "1"; then
    AC_DEFINE(HAVE_SETRESUID, 1, [whether we have setresuid()])
fi

AC_ARG_WITH(nix-user, AC_HELP_STRING([--with-nix-user=USER],
  [user for Nix setuid binaries]),
  NIX_USER=$withval, NIX_USER=nix)
AC_SUBST(NIX_USER)
AC_DEFINE_UNQUOTED(NIX_USER, ["$NIX_USER"], [Nix user])
                                                      
AC_ARG_WITH(nix-group, AC_HELP_STRING([--with-nix-group=USER],
  [group for Nix setuid binaries]),
  NIX_GROUP=$withval, NIX_GROUP=nix)
AC_SUBST(NIX_GROUP)
AC_DEFINE_UNQUOTED(NIX_GROUP, ["$NIX_GROUP"], [Nix group])

                                                                                                            
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
   src/bsdiff-4.2/Makefile
   scripts/Makefile
   corepkgs/Makefile
   corepkgs/nar/Makefile
   corepkgs/buildenv/Makefile
   corepkgs/channels/Makefile
   doc/Makefile
   doc/manual/Makefile
   misc/Makefile
   misc/emacs/Makefile
   tests/Makefile
  ])
AC_OUTPUT
