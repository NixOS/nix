AC_INIT(nix, m4_esyscmd([bash -c "echo -n $(cat ./version)$VERSION_SUFFIX"]))
AC_CONFIG_SRCDIR(README.md)
AC_CONFIG_AUX_DIR(config)

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
     armv6|armv7)
        machine_name="${host_cpu}l";;
     *)
        machine_name="$host_cpu";;
   esac

   case "$host_os" in
     linux-gnu*|linux-musl*)
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


# Solaris-specific stuff.
AC_STRUCT_DIRENT_D_TYPE
if test "$sys_name" = sunos; then
    # Solaris requires -lsocket -lnsl for network functions
    LIBS="-lsocket -lnsl $LIBS"
fi


CFLAGS=
CXXFLAGS=
AC_PROG_CC
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_11


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


AC_CHECK_FUNCS([statvfs pipe2])


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
AC_PATH_PROG(flex, flex, false)
AC_PATH_PROG(bison, bison, false)
NEED_PROG(perl, perl)
NEED_PROG(sed, sed)
NEED_PROG(tar, tar)
NEED_PROG(bzip2, bzip2)
NEED_PROG(gzip, gzip)
NEED_PROG(xz, xz)
AC_PATH_PROG(dot, dot)
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
AC_SUBST(perllibdir, [${libdir}/perl5/site_perl/$perlversion/$perlarchname])
AC_MSG_RESULT($perllibdir)


NEED_PROG(cat, cat)
NEED_PROG(tr, tr)
AC_ARG_WITH(coreutils-bin, AC_HELP_STRING([--with-coreutils-bin=PATH],
  [path of cat, mkdir, etc.]),
  coreutils=$withval, coreutils=$(dirname $cat))
AC_SUBST(coreutils)


AC_ARG_WITH(store-dir, AC_HELP_STRING([--with-store-dir=PATH],
  [path of the Nix store (defaults to /nix/store)]),
  storedir=$withval, storedir='/nix/store')
AC_SUBST(storedir)


# Look for OpenSSL, a required dependency.
PKG_CHECK_MODULES([OPENSSL], [libcrypto], [CXXFLAGS="$OPENSSL_CFLAGS $CXXFLAGS"])


# Look for libbz2, a required dependency.
AC_CHECK_LIB([bz2], [BZ2_bzWriteOpen], [true],
  [AC_MSG_ERROR([Nix requires libbz2, which is part of bzip2.  See http://www.bzip.org/.])])
AC_CHECK_HEADERS([bzlib.h], [true],
  [AC_MSG_ERROR([Nix requires libbz2, which is part of bzip2.  See http://www.bzip.org/.])])


# Look for SQLite, a required dependency.
PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.6.19], [CXXFLAGS="$SQLITE3_CFLAGS $CXXFLAGS"])


# Look for libcurl, a required dependency.
PKG_CHECK_MODULES([LIBCURL], [libcurl], [CXXFLAGS="$LIBCURL_CFLAGS $CXXFLAGS"])


# Look for libsodium, an optional dependency.
PKG_CHECK_MODULES([SODIUM], [libsodium],
  [AC_DEFINE([HAVE_SODIUM], [1], [Whether to use libsodium for cryptography.])
   CXXFLAGS="$SODIUM_CFLAGS $CXXFLAGS"
   have_sodium=1], [have_sodium=])
AC_SUBST(HAVE_SODIUM, [$have_sodium])


# Look for liblzma, a required dependency.
PKG_CHECK_MODULES([LIBLZMA], [liblzma], [CXXFLAGS="$LIBLZMA_CFLAGS $CXXFLAGS"])


# Look for aws-cpp-sdk-s3.
AC_LANG_PUSH(C++)
CPPFLAGS="$CPPFLAGS -std=c++0x"
AC_CHECK_HEADERS([aws/s3/S3Client.h],
  [AC_DEFINE([ENABLE_S3], [1], [Whether to enable S3 support via aws-cpp-sdk-s3.])
  enable_s3=1], [enable_s3=])
AC_SUBST(ENABLE_S3, [$enable_s3])
AC_LANG_POP(C++)

# Look for cpp-ipfs-api
AC_LANG_PUSH(C++)
CPPFLAGS="$CPPFLAGS -std=c++0x"
AC_CHECK_HEADERS([ipfs/client.h],
  [AC_DEFINE([ENABLE_IPFS], [1], [Whether to enable IPFS support via cpp-ipfs-api.])
  enable_ipfs=1], [enable_ipfs=])
AC_SUBST(ENABLE_IPFS, [$enable_ipfs])
AC_LANG_POP(C++)

# Whether to use the Boehm garbage collector.
AC_ARG_ENABLE(gc, AC_HELP_STRING([--enable-gc],
  [enable garbage collection in the Nix expression evaluator (requires Boehm GC) [default=no]]),
  gc=$enableval, gc=no)
if test "$gc" = yes; then
  PKG_CHECK_MODULES([BDW_GC], [bdw-gc])
  CXXFLAGS="$BDW_GC_CFLAGS $CXXFLAGS"
  AC_DEFINE(HAVE_BOEHMGC, 1, [Whether to use the Boehm garbage collector.])
fi


# Check for the required Perl dependencies (DBI, DBD::SQLite).
perlFlags="-I$perllibdir"

AC_ARG_WITH(dbi, AC_HELP_STRING([--with-dbi=PATH],
  [prefix of the Perl DBI library]),
  perlFlags="$perlFlags -I$withval")

AC_ARG_WITH(dbd-sqlite, AC_HELP_STRING([--with-dbd-sqlite=PATH],
  [prefix of the Perl DBD::SQLite library]),
  perlFlags="$perlFlags -I$withval")

AC_MSG_CHECKING([whether DBD::SQLite works])
if ! $perl $perlFlags -e 'use DBI; use DBD::SQLite;' 2>&5; then
    AC_MSG_RESULT(no)
    AC_MSG_FAILURE([The Perl modules DBI and/or DBD::SQLite are missing.])
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
AC_SUBST(perlbindings)
AC_MSG_RESULT($perlbindings)


AC_ARG_ENABLE(init-state, AC_HELP_STRING([--disable-init-state],
  [do not initialise DB etc. in `make install']),
  init_state=$enableval, init_state=yes)
#AM_CONDITIONAL(INIT_STATE, test "$init_state" = "yes")


# documentation generation switch
AC_ARG_ENABLE(doc-gen, AC_HELP_STRING([--disable-doc-gen],
  [disable documentation generation]),
  doc_generate=$enableval, doc_generate=yes)
AC_SUBST(doc_generate)


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


# Expand all variables in config.status.
test "$prefix" = NONE && prefix=$ac_default_prefix
test "$exec_prefix" = NONE && exec_prefix='${prefix}'
for name in $ac_subst_vars; do
    declare $name="$(eval echo "${!name}")"
    declare $name="$(eval echo "${!name}")"
    declare $name="$(eval echo "${!name}")"
done

rm -f Makefile.config

AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([])
AC_OUTPUT
