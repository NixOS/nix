AC_INIT(nix, m4_esyscmd([echo -n $(cat ./version)$VERSION_SUFFIX]))
AC_CONFIG_SRCDIR(README)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE([dist-bzip2 foreign])

AC_DEFINE_UNQUOTED(NIX_VERSION, ["$VERSION"], [Nix version.])

AC_PROG_SED

# Construct a Nix system name (like "i686-linux").
AC_CANONICAL_HOST
AC_MSG_CHECKING([for the canonical Nix system name])

AC_ARG_WITH(system, AC_HELP_STRING([--with-system=SYSTEM],
  [Platform identifier (e.g., `i686-linux').]),
  [system=$withval],
  [case "$host_cpu" in
     i*86)
        machine_name="i686";;
     amd64)
        machine_name="x86_64";;
     *)
        machine_name="$host_cpu";;
   esac

   case "$host_os" in
     linux-gnu*)
        # For backward compatibility, strip the `-gnu' part.
        system="$machine_name-linux";;
     *)
        # Strip the version number from names such as `gnu0.3',
        # `darwin10.2.0', etc.
        system="$machine_name-`echo $host_os | "$SED" -e's/@<:@0-9.@:>@*$//g'`";;
   esac])

sys_name=$(uname -s | tr 'A-Z ' 'a-z_')

case $sys_name in
    cygwin*)
        sys_name=cygwin
        ;;
esac

AC_MSG_RESULT($system)
AC_SUBST(system)
AC_DEFINE_UNQUOTED(SYSTEM, ["$system"], [platform identifier (`cpu-os')])


# State should be stored in /nix/var, unless the user overrides it explicitly.
test "$localstatedir" = '${prefix}/var' && localstatedir=/nix/var


# Windows-specific stuff.  On Cygwin, dynamically linking against the
# ATerm DLL works, except that it requires the ATerm "lib" directory
# to be in $PATH, as Windows doesn't have anything like an RPATH
# embedded in executable.  Since this is kind of annoying, we use
# static libraries for now.
if test "$sys_name" = "cygwin"; then
    AC_DISABLE_SHARED
    AC_ENABLE_STATIC
fi


# Solaris-specific stuff.
if test "$sys_name" = sunos; then
    # Solaris requires -lsocket -lnsl for network functions
    LIBS="-lsocket -lnsl $LIBS"
fi


AC_PROG_CC
AC_PROG_CXX

# To build programs to be run in the build machine.
if test "$CC_FOR_BUILD" = ""; then
    if test "$cross_compiling" = "yes"; then
        AC_CHECK_PROGS(CC_FOR_BUILD, gcc cc)
    else
        CC_FOR_BUILD="$CC"
    fi
fi
AC_SUBST([CC_FOR_BUILD])

# We are going to use libtool.
AC_DISABLE_STATIC
AC_ENABLE_SHARED
AC_PROG_LIBTOOL


# Use 64-bit file system calls so that we can support files > 2 GiB.
AC_SYS_LARGEFILE


# Check for pubsetbuf.
AC_MSG_CHECKING([for pubsetbuf])
AC_LANG_PUSH(C++)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <iostream>
using namespace std;
static char buf[1024];]],
    [[cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));]])],
    [AC_MSG_RESULT(yes) AC_DEFINE(HAVE_PUBSETBUF, 1, [Whether pubsetbuf is available.])],
    AC_MSG_RESULT(no))
AC_LANG_POP(C++)


# Check for chroot support (requires chroot() and bind mounts).
AC_CHECK_FUNCS([chroot])
AC_CHECK_FUNCS([unshare])
AC_CHECK_HEADERS([sched.h])
AC_CHECK_HEADERS([sys/param.h])
AC_CHECK_HEADERS([sys/mount.h], [], [],
[#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
# endif
])


# Check for vfork.
#AC_FUNC_FORK()


# Check for lutimes, optionally used for changing the mtime of
# symlinks.
AC_CHECK_FUNCS([lutimes])


# Check whether the store optimiser can optimise symlinks.
AC_MSG_CHECKING([whether it is possible to create a link to a symlink])
ln -s bla tmp_link
if ln tmp_link tmp_link2 2> /dev/null; then
    AC_MSG_RESULT(yes)
    AC_DEFINE(CAN_LINK_SYMLINK, 1, [Whether link() works on symlinks.])
else
    AC_MSG_RESULT(no)
fi
rm -f tmp_link tmp_link2


# Check for <locale>.
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([locale])
AC_LANG_POP(C++)


# Check for <err.h>.
AC_CHECK_HEADER([err.h], [], [bsddiff_compat_include="-Icompat-include"])
AC_SUBST([bsddiff_compat_include])


# Check whether we have the personality() syscall, which allows us to
# do i686-linux builds on x86_64-linux machines.
AC_CHECK_HEADERS([sys/personality.h])


# Check for <linux/fs.h> (for immutable file support).
AC_CHECK_HEADERS([linux/fs.h])


# Check for tr1/unordered_set.
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([tr1/unordered_set])
AC_LANG_POP(C++)


AC_DEFUN([NEED_PROG],
[
AC_PATH_PROG($1, $2)
if test -z "$$1"; then
    AC_MSG_ERROR([$2 is required])
fi
])

NEED_PROG(curl, curl)
NEED_PROG(bash, bash)
NEED_PROG(patch, patch)
AC_PATH_PROG(xmllint, xmllint, false)
AC_PATH_PROG(xsltproc, xsltproc, false)
AC_PATH_PROG(w3m, w3m, false)
AC_PATH_PROG(flex, flex, false)
AC_PATH_PROG(bison, bison, false)
NEED_PROG(perl, perl)
NEED_PROG(sed, sed)
NEED_PROG(tar, tar)
NEED_PROG(bzip2, bzip2)
NEED_PROG(xz, xz)
AC_PATH_PROG(dot, dot)
AC_PATH_PROG(dblatex, dblatex)
AC_PATH_PROG(gzip, gzip)
AC_PATH_PROG(pv, pv, pv)


# Test that Perl has the open/fork feature (Perl 5.8.0 and beyond).
AC_MSG_CHECKING([whether Perl is recent enough])
if ! $perl -e 'open(FOO, "-|", "true"); while (<FOO>) { print; }; close FOO or die;'; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([Your Perl version is too old.  Nix requires Perl 5.8.0 or newer.])
fi
AC_MSG_RESULT(yes)


# Figure out where to install Perl modules.
AC_MSG_CHECKING([for the Perl installation prefix])
perlversion=$($perl -e 'use Config; print $Config{version};')
perlarchname=$($perl -e 'use Config; print $Config{archname};')
AC_SUBST(perllibdir, [$\(libdir\)/perl5/site_perl/$perlversion/$perlarchname])
AC_MSG_RESULT($perllibdir)


NEED_PROG(cat, cat)
NEED_PROG(tr, tr)
AC_ARG_WITH(coreutils-bin, AC_HELP_STRING([--with-coreutils-bin=PATH],
  [path of cat, mkdir, etc.]),
  coreutils=$withval, coreutils=$(dirname $cat))
AC_SUBST(coreutils)


AC_ARG_WITH(docbook-rng, AC_HELP_STRING([--with-docbook-rng=PATH],
  [path of the DocBook RelaxNG schema]),
  docbookrng=$withval, docbookrng=/docbook-rng-missing)
AC_SUBST(docbookrng)

AC_ARG_WITH(docbook-xsl, AC_HELP_STRING([--with-docbook-xsl=PATH],
  [path of the DocBook XSL stylesheets]),
  docbookxsl=$withval, docbookxsl=/docbook-xsl-missing)
AC_SUBST(docbookxsl)


AC_ARG_WITH(xml-flags, AC_HELP_STRING([--with-xml-flags=FLAGS],
  [extra flags to be passed to xmllint and xsltproc]),
  xmlflags=$withval, xmlflags=)
AC_SUBST(xmlflags)


AC_ARG_WITH(store-dir, AC_HELP_STRING([--with-store-dir=PATH],
  [path of the Nix store (defaults to /nix/store)]),
  storedir=$withval, storedir='/nix/store')
AC_SUBST(storedir)


# Look for OpenSSL, an optional dependency.
AC_PATH_PROG(openssl_prog, openssl, openssl) # if not found, call openssl in $PATH
AC_SUBST(openssl_prog)
AC_DEFINE_UNQUOTED(OPENSSL_PATH, ["$openssl_prog"], [Path of the OpenSSL binary])

PKG_CHECK_MODULES([OPENSSL], [libcrypto],
  [AC_DEFINE([HAVE_OPENSSL], [1], [Whether to use OpenSSL.])
   CXXFLAGS="$OPENSSL_CFLAGS $CXXFLAGS"
   have_openssl=1], [true])
AM_CONDITIONAL(HAVE_OPENSSL, test "$have_openssl" = 1)


# Look for libbz2, a required dependency.
AC_CHECK_LIB([bz2], [BZ2_bzWriteOpen], [true],
  [AC_MSG_ERROR([Nix requires libbz2, which is part of bzip2.  See http://www.bzip.org/.])])
AC_CHECK_HEADERS([bzlib.h], [true],
  [AC_MSG_ERROR([Nix requires libbz2, which is part of bzip2.  See http://www.bzip.org/.])])


# Look for SQLite, a required dependency.
PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.6.19], [CXXFLAGS="$SQLITE3_CFLAGS $CXXFLAGS"])


# Whether to use the Boehm garbage collector.
AC_ARG_ENABLE(gc, AC_HELP_STRING([--enable-gc],
  [enable garbage collection in the Nix expression evaluator (requires Boehm GC) [default=no]]),
  gc=$enableval, gc=no)
if test "$gc" = yes; then
  PKG_CHECK_MODULES([BDW_GC], [bdw-gc])
  CXXFLAGS="$BDW_GC_CFLAGS $CXXFLAGS"
  AC_DEFINE(HAVE_BOEHMGC, 1, [Whether to use the Boehm garbage collector.])
fi


# Check for the required Perl dependencies (DBI, DBD::SQLite and WWW::Curl).
perlFlags="-I$perllibdir"

AC_ARG_WITH(dbi, AC_HELP_STRING([--with-dbi=PATH],
  [prefix of the Perl DBI library]),
  perlFlags="$perlFlags -I$withval")

AC_ARG_WITH(dbd-sqlite, AC_HELP_STRING([--with-dbd-sqlite=PATH],
  [prefix of the Perl DBD::SQLite library]),
  perlFlags="$perlFlags -I$withval")

AC_ARG_WITH(www-curl, AC_HELP_STRING([--with-www-curl=PATH],
  [prefix of the Perl WWW::Curl library]),
  perlFlags="$perlFlags -I$withval")

AC_MSG_CHECKING([whether DBD::SQLite works])
if ! $perl $perlFlags -e 'use DBI; use DBD::SQLite;' 2>&5; then
    AC_MSG_RESULT(no)
    AC_MSG_FAILURE([The Perl modules DBI and/or DBD::SQLite are missing.])
fi
AC_MSG_RESULT(yes)

AC_MSG_CHECKING([whether WWW::Curl works])
if ! $perl $perlFlags -e 'use WWW::Curl;' 2>&5; then
    AC_MSG_RESULT(no)
    AC_MSG_FAILURE([The Perl module WWW::Curl is missing.])
fi
AC_MSG_RESULT(yes)

AC_SUBST(perlFlags)


# Whether to build the Perl bindings
AC_MSG_CHECKING([whether to build the Perl bindings])
AC_ARG_ENABLE(perl-bindings, AC_HELP_STRING([--enable-perl-bindings],
  [whether to build the Perl bindings (recommended) [default=yes]]),
  perlbindings=$enableval, perlbindings=yes)
if test "$enable_shared" = no; then
   # Perl bindings require shared libraries.
   perlbindings=no
fi
AM_CONDITIONAL(PERL_BINDINGS, test "$perlbindings" = "yes")
AC_SUBST(perlbindings)
AC_MSG_RESULT($perlbindings)


AC_ARG_ENABLE(init-state, AC_HELP_STRING([--disable-init-state],
  [do not initialise DB etc. in `make install']),
  init_state=$enableval, init_state=yes)
AM_CONDITIONAL(INIT_STATE, test "$init_state" = "yes")


# Setuid installations.
AC_CHECK_FUNCS([setresuid setreuid lchown])


# Nice to have, but not essential.
AC_CHECK_FUNCS([strsignal posix_fallocate nanosleep sysconf])


# This is needed if bzip2 is a static library, and the Nix libraries
# are dynamic.
if test "$(uname)" = "Darwin"; then
    LDFLAGS="-all_load $LDFLAGS"
fi


# Figure out the extension of dynamic libraries.
eval dynlib_suffix=$shrext_cmds
AC_SUBST(dynlib_suffix)


# Do we have GNU tar?
AC_MSG_CHECKING([if you have a recent GNU tar])
if $tar --version 2> /dev/null | grep -q GNU && tar cvf /dev/null --warning=no-timestamp ./config.log > /dev/null; then
    AC_MSG_RESULT(yes)
    tarFlags="--warning=no-timestamp"
else
    AC_MSG_RESULT(no)
fi
AC_SUBST(tarFlags)


AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile
   src/Makefile
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
   src/nix-daemon/Makefile
   src/nix-setuid-helper/Makefile
   src/nix-log2xml/Makefile
   src/bsdiff-4.3/Makefile
   perl/Makefile
   scripts/Makefile
   corepkgs/Makefile
   doc/Makefile
   doc/manual/Makefile
   misc/Makefile
   misc/emacs/Makefile
   tests/Makefile
  ])
AC_OUTPUT
