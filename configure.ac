AC_INIT(nix, m4_esyscmd([echo -n $(cat ./version)$VERSION_SUFFIX]))
AC_CONFIG_SRCDIR(README)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE([dist-bzip2 foreign])

AC_DEFINE_UNQUOTED(NIX_VERSION, ["$VERSION"], [Nix version.])

AC_CANONICAL_HOST


# Construct a Nix system name (like "i686-linux").
AC_MSG_CHECKING([for the canonical Nix system name])
cpu_name=$(uname -p | tr 'A-Z ' 'a-z_')
machine_name=$(uname -m | tr 'A-Z ' 'a-z_')

case $machine_name in
    i*86)
        machine_name=i686
        ;;
    x86_64)
        machine_name=x86_64
        ;;
    ppc)
        machine_name=powerpc
        ;;
    *)
        if test "$cpu_name" != "unknown"; then
            machine_name=$cpu_name
        fi
        ;;
esac

sys_name=$(uname -s | tr 'A-Z ' 'a-z_')

case $sys_name in
    cygwin*)
        sys_name=cygwin
        ;;
esac

AC_ARG_WITH(system, AC_HELP_STRING([--with-system=SYSTEM],
  [Platform identifier (e.g., `i686-linux').]),
  system=$withval, system="${machine_name}-${sys_name}")
AC_MSG_RESULT($system)
AC_SUBST(system)
AC_DEFINE_UNQUOTED(SYSTEM, ["$system"], [platform identifier (`cpu-os')])


# State should be stored in /nix/var, unless the user overrides it explicitly.
test "$localstatedir" = '${prefix}/var' && localstatedir=/nix/var


# Whether to produce a statically linked binary.  On Cygwin, this is
# the default: dynamically linking against the ATerm DLL does work,
# except that it requires the ATerm "lib" directory to be in $PATH, as
# Windows doesn't have anything like an RPATH embedded in executable.
# Since this is kind of annoying, we use static libraries for now.

AC_ARG_ENABLE(static-nix, AC_HELP_STRING([--enable-static-nix],
  [produce statically linked binaries]),
  static_nix=$enableval, static_nix=no)

if test "$sys_name" = cygwin; then
   static_nix=yes
fi

if test "$static_nix" = yes; then
    AC_DISABLE_SHARED
    AC_ENABLE_STATIC
fi


# Windows-specific stuff.
if test "$sys_name" = "cygwin"; then
    # We cannot delete open files.
    AC_DEFINE(CANNOT_DELETE_OPEN_FILES, 1, [Whether it is impossible to delete open files.])
fi

# Solaris-specific stuff.
if test "$sys_name" = "sunos"; then
    # Solaris requires -lsocket -lnsl for network functions
    ADDITIONAL_NETWORK_LIBS="-lsocket -lnsl"
    AC_SUBST(ADDITIONAL_NETWORK_LIBS)
fi

AC_PROG_CC
AC_PROG_CXX


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
AC_CHECK_HEADERS([sched.h], [], [], [])
AC_CHECK_HEADERS([sys/param.h], [], [], [])
AC_CHECK_HEADERS([sys/mount.h], [], [],
[#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
# endif
])


# Check for <locale>.
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([locale], [], [], [])
AC_LANG_POP(C++)


# Check whether we have the personality() syscall, which allows us to
# do i686-linux builds on x86_64-linux machines.
AC_CHECK_HEADERS([sys/personality.h])


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
NEED_PROG(tar, tar)
AC_PATH_PROG(dot, dot)
AC_PATH_PROG(dblatex, dblatex)
AC_PATH_PROG(gzip, gzip)

AC_PATH_PROG(openssl_prog, openssl, openssl) # if not found, call openssl in $PATH
AC_SUBST(openssl_prog)
AC_DEFINE_UNQUOTED(OPENSSL_PATH, ["$openssl_prog"], [Path of the OpenSSL binary])

# Test that Perl has the open/fork feature (Perl 5.8.0 and beyond).
AC_MSG_CHECKING([whether Perl is recent enough])
if ! $perl -e 'open(FOO, "-|", "true"); while (<FOO>) { print; }; close FOO or die;'; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([Your Perl version is too old.  Nix requires Perl 5.8.0 or newer.])
fi
AC_MSG_RESULT(yes)

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
  [path of the Nix store]),
  storedir=$withval, storedir='/nix/store')
AC_SUBST(storedir)

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

AC_ARG_WITH(openssl, AC_HELP_STRING([--with-openssl=PATH],
  [prefix of the OpenSSL library]),
  openssl=$withval, openssl=)
AM_CONDITIONAL(HAVE_OPENSSL, test -n "$openssl")
if test -n "$openssl"; then
  LDFLAGS="-L$openssl/lib -lcrypto $LDFLAGS"
  CFLAGS="-I$openssl/include $CFLAGS"
  CXXFLAGS="-I$openssl/include $CXXFLAGS"
  AC_DEFINE(HAVE_OPENSSL, 1, [Whether to use OpenSSL.])
fi

AC_ARG_WITH(bzip2, AC_HELP_STRING([--with-bzip2=PATH],
  [prefix of bzip2]),
  bzip2=$withval, bzip2=)
AM_CONDITIONAL(HAVE_BZIP2, test -n "$bzip2")
if test -z "$bzip2"; then
  # Headers and libraries will be used from the temporary installation
  # in externals/inst-bzip2.
  bzip2_lib='-L${top_builddir}/externals/inst-bzip2/lib -lbz2'
  bzip2_include='-I${top_builddir}/externals/inst-bzip2/include'
  # The binary will be copied to $libexecdir.
  bzip2_bin='${libexecdir}/nix'
  # But for testing, we have to use the temporary copy :-(
  bzip2_bin_test='${top_builddir}/externals/inst-bzip2/bin'
else
  bzip2_lib="-L$bzip2/lib -lbz2"
  bzip2_include="-I$bzip2/include"
  bzip2_bin="$bzip2/bin"
  bzip2_bin_test="$bzip2/bin"
fi
AC_SUBST(bzip2_lib)
AC_SUBST(bzip2_include)
AC_SUBST(bzip2_bin)
AC_SUBST(bzip2_bin_test)


AC_CHECK_LIB(pthread, pthread_mutex_init)


AC_ARG_ENABLE(init-state, AC_HELP_STRING([--disable-init-state],
  [do not initialise DB etc. in `make install']),
  init_state=$enableval, init_state=yes)
AM_CONDITIONAL(INIT_STATE, test "$init_state" = "yes")


# Setuid installations.
AC_CHECK_FUNCS([setresuid setreuid lchown])


# Nice to have, but not essential.
AC_CHECK_FUNCS([strsignal])
AC_CHECK_FUNCS([posix_fallocate])


# This is needed if ATerm or bzip2 are static libraries,
# and the Nix libraries are dynamic.
if test "$(uname)" = "Darwin"; then
    LDFLAGS="-all_load $LDFLAGS"
fi


if test "$static_nix" = yes; then
    # `-all-static' has to be added at the end of configure, because
    # the C compiler doesn't know about -all-static (it's filtered out
    # by libtool, but configure doesn't use libtool).
    LDFLAGS="-all-static $LDFLAGS"
fi


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
   src/nix-worker/Makefile
   src/nix-setuid-helper/Makefile
   src/nix-log2xml/Makefile
   src/bsdiff-4.3/Makefile
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
