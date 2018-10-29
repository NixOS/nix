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
AC_PROG_CPP
AX_CXX_COMPILE_STDCXX_14


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

NEED_PROG(bash, bash)
NEED_PROG(patch, patch)
AC_PATH_PROG(xmllint, xmllint, false)
AC_PATH_PROG(xsltproc, xsltproc, false)
AC_PATH_PROG(flex, flex, false)
AC_PATH_PROG(bison, bison, false)
NEED_PROG(sed, sed)
NEED_PROG(tar, tar)
NEED_PROG(bzip2, bzip2)
NEED_PROG(gzip, gzip)
NEED_PROG(xz, xz)
AC_PATH_PROG(dot, dot)
AC_PATH_PROG(pv, pv, pv)
AC_PATH_PROGS(brotli, brotli bro, bro)
AC_PATH_PROG(lsof, lsof, lsof)


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
  [AC_MSG_ERROR([Nix requires libbz2, which is part of bzip2.  See https://web.archive.org/web/20180624184756/http://www.bzip.org/.])])
AC_CHECK_HEADERS([bzlib.h], [true],
  [AC_MSG_ERROR([Nix requires libbz2, which is part of bzip2.  See https://web.archive.org/web/20180624184756/http://www.bzip.org/.])])


# Look for SQLite, a required dependency.
PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.6.19], [CXXFLAGS="$SQLITE3_CFLAGS $CXXFLAGS"])


# Look for libcurl, a required dependency.
PKG_CHECK_MODULES([LIBCURL], [libcurl], [CXXFLAGS="$LIBCURL_CFLAGS $CXXFLAGS"])

# Look for editline, a required dependency.
PKG_CHECK_MODULES([EDITLINE], [libeditline], [CXXFLAGS="$EDITLINE_CFLAGS $CXXFLAGS"])

# Look for libsodium, an optional dependency.
PKG_CHECK_MODULES([SODIUM], [libsodium],
  [AC_DEFINE([HAVE_SODIUM], [1], [Whether to use libsodium for cryptography.])
   CXXFLAGS="$SODIUM_CFLAGS $CXXFLAGS"
   have_sodium=1], [have_sodium=])
AC_SUBST(HAVE_SODIUM, [$have_sodium])


# Look for liblzma, a required dependency.
PKG_CHECK_MODULES([LIBLZMA], [liblzma], [CXXFLAGS="$LIBLZMA_CFLAGS $CXXFLAGS"])
AC_CHECK_LIB([lzma], [lzma_stream_encoder_mt],
  [AC_DEFINE([HAVE_LZMA_MT], [1], [xz multithreaded compression support])])


# Look for libbrotli{enc,dec}.
PKG_CHECK_MODULES([LIBBROTLI], [libbrotlienc libbrotlidec], [CXXFLAGS="$LIBBROTLI_CFLAGS $CXXFLAGS"])


# Look for libseccomp, required for Linux sandboxing.
if test "$sys_name" = linux; then
  AC_ARG_ENABLE([seccomp-sandboxing],
                AC_HELP_STRING([--disable-seccomp-sandboxing],
                               [Don't build support for seccomp sandboxing (only recommended if your arch doesn't support libseccomp yet!)]
                              ))
  if test "x$enable_seccomp_sandboxing" != "xno"; then
    PKG_CHECK_MODULES([LIBSECCOMP], [libseccomp],
                      [CXXFLAGS="$LIBSECCOMP_CFLAGS $CXXFLAGS"])
    have_seccomp=1
    AC_DEFINE([HAVE_SECCOMP], [1], [Whether seccomp is available and should be used for sandboxing.])
  else
    have_seccomp=
  fi
else
  have_seccomp=
fi
AC_SUBST(HAVE_SECCOMP, [$have_seccomp])


# Look for aws-cpp-sdk-s3.
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([aws/s3/S3Client.h],
  [AC_DEFINE([ENABLE_S3], [1], [Whether to enable S3 support via aws-sdk-cpp.])
  enable_s3=1], [enable_s3=])
AC_SUBST(ENABLE_S3, [$enable_s3])
AC_LANG_POP(C++)

if test -n "$enable_s3"; then
  declare -a aws_version_tokens=($(printf '#include <aws/core/VersionConfig.h>\nAWS_SDK_VERSION_STRING' | $CPP $CPPFLAGS - | grep -v '^#.*' | sed 's/"//g' | tr '.' ' '))
  AC_DEFINE_UNQUOTED([AWS_VERSION_MAJOR], ${aws_version_tokens@<:@0@:>@}, [Major version of aws-sdk-cpp.])
  AC_DEFINE_UNQUOTED([AWS_VERSION_MINOR], ${aws_version_tokens@<:@1@:>@}, [Minor version of aws-sdk-cpp.])
fi


# Whether to use the Boehm garbage collector.
AC_ARG_ENABLE(gc, AC_HELP_STRING([--enable-gc],
  [enable garbage collection in the Nix expression evaluator (requires Boehm GC) [default=no]]),
  gc=$enableval, gc=no)
if test "$gc" = yes; then
  PKG_CHECK_MODULES([BDW_GC], [bdw-gc])
  CXXFLAGS="$BDW_GC_CFLAGS $CXXFLAGS"
  AC_DEFINE(HAVE_BOEHMGC, 1, [Whether to use the Boehm garbage collector.])
fi


# documentation generation switch
AC_ARG_ENABLE(doc-gen, AC_HELP_STRING([--disable-doc-gen],
  [disable documentation generation]),
  doc_generate=$enableval, doc_generate=yes)
AC_SUBST(doc_generate)


# Setuid installations.
AC_CHECK_FUNCS([setresuid setreuid lchown])


# Nice to have, but not essential.
AC_CHECK_FUNCS([strsignal posix_fallocate sysconf])


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


AC_ARG_WITH(sandbox-shell, AC_HELP_STRING([--with-sandbox-shell=PATH],
  [path of a statically-linked shell to use as /bin/sh in sandboxes]),
  sandbox_shell=$withval)
AC_SUBST(sandbox_shell)


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
