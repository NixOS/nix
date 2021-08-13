AC_INIT([nix],[m4_esyscmd(bash -c "echo -n $(cat ./.version)$VERSION_SUFFIX")])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR(README.md)
AC_CONFIG_AUX_DIR(config)

AC_PROG_SED

# Construct a Nix system name (like "i686-linux").
AC_CANONICAL_HOST
AC_MSG_CHECKING([for the canonical Nix system name])

AC_ARG_WITH(system, AS_HELP_STRING([--with-system=SYSTEM],[Platform identifier (e.g., `i686-linux').]),
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

AC_MSG_RESULT($system)
AC_SUBST(system)
AC_DEFINE_UNQUOTED(SYSTEM, ["$system"], [platform identifier ('cpu-os')])


# State should be stored in /nix/var, unless the user overrides it explicitly.
test "$localstatedir" = '${prefix}/var' && localstatedir=/nix/var


CFLAGS=
CXXFLAGS=
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP

AC_CHECK_TOOL([AR], [ar])

# Use 64-bit file system calls so that we can support files > 2 GiB.
AC_SYS_LARGEFILE


# Solaris-specific stuff.
AC_STRUCT_DIRENT_D_TYPE
case "$host_os" in
  solaris*)
    # Solaris requires -lsocket -lnsl for network functions
    LDFLAGS="-lsocket -lnsl $LDFLAGS"
    ;;
esac


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
AC_PATH_PROG(flex, flex, false)
AC_PATH_PROG(bison, bison, false)
AC_PATH_PROG(dot, dot)
AC_PATH_PROG(lsof, lsof, lsof)
NEED_PROG(jq, jq)


AC_SUBST(coreutils, [$(dirname $(type -p cat))])


AC_ARG_WITH(store-dir, AS_HELP_STRING([--with-store-dir=PATH],[path of the Nix store (defaults to /nix/store)]),
  storedir=$withval, storedir='/nix/store')
AC_SUBST(storedir)


# Look for boost, a required dependency.
# Note that AX_BOOST_BASE only exports *CPP* BOOST_CPPFLAGS, no CXX flags,
# and CPPFLAGS are not passed to the C++ compiler automatically.
# Thus we append the returned CPPFLAGS to the CXXFLAGS here.
AX_BOOST_BASE([1.66], [CXXFLAGS="$BOOST_CPPFLAGS $CXXFLAGS"], [AC_MSG_ERROR([Nix requires boost.])])
# For unknown reasons, setting this directly in the ACTION-IF-FOUND above
# ends up with LDFLAGS being empty, so we set it afterwards.
LDFLAGS="$BOOST_LDFLAGS $LDFLAGS"

# On some platforms, new-style atomics need a helper library
AC_MSG_CHECKING(whether -latomic is needed)
AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <stdint.h>
uint64_t v;
int main() {
    return (int)__atomic_load_n(&v, __ATOMIC_ACQUIRE);
}]])], GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC=no, GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC=yes)
AC_MSG_RESULT($GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC)
if test "x$GCC_ATOMIC_BUILTINS_NEED_LIBATOMIC" = xyes; then
    LDFLAGS="-latomic $LDFLAGS"
fi

PKG_PROG_PKG_CONFIG

AC_ARG_ENABLE(shared, AS_HELP_STRING([--enable-shared],[Build shared libraries for Nix [default=yes]]),
  shared=$enableval, shared=yes)
if test "$shared" = yes; then
  AC_SUBST(BUILD_SHARED_LIBS, 1, [Whether to build shared libraries.])
else
  AC_SUBST(BUILD_SHARED_LIBS, 0, [Whether to build shared libraries.])
  PKG_CONFIG="$PKG_CONFIG --static"
fi

# Look for OpenSSL, a required dependency. FIXME: this is only (maybe)
# used by S3BinaryCacheStore.
PKG_CHECK_MODULES([OPENSSL], [libcrypto], [CXXFLAGS="$OPENSSL_CFLAGS $CXXFLAGS"])


# Checks for libarchive
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive >= 3.1.2], [CXXFLAGS="$LIBARCHIVE_CFLAGS $CXXFLAGS"])
# Workaround until https://github.com/libarchive/libarchive/issues/1446 is fixed
if test "$shared" != yes; then
    LIBARCHIVE_LIBS+=' -lz'
fi

# Look for SQLite, a required dependency.
PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.6.19], [CXXFLAGS="$SQLITE3_CFLAGS $CXXFLAGS"])

# Look for libcurl, a required dependency.
PKG_CHECK_MODULES([LIBCURL], [libcurl], [CXXFLAGS="$LIBCURL_CFLAGS $CXXFLAGS"])

# Look for editline, a required dependency.
# The the libeditline.pc file was added only in libeditline >= 1.15.2,
# see https://github.com/troglobit/editline/commit/0a8f2ef4203c3a4a4726b9dd1336869cd0da8607,
# but e.g. Ubuntu 16.04 has an older version, so we fall back to searching for
# editline.h when the pkg-config approach fails.
PKG_CHECK_MODULES([EDITLINE], [libeditline], [CXXFLAGS="$EDITLINE_CFLAGS $CXXFLAGS"], [
  AC_CHECK_HEADERS([editline.h], [true],
    [AC_MSG_ERROR([Nix requires libeditline; it was found neither via pkg-config nor its normal header.])])
  AC_SEARCH_LIBS([readline read_history], [editline], [],
    [AC_MSG_ERROR([Nix requires libeditline; it was not found via pkg-config, but via its header, but required functions do not work. Maybe it is too old? >= 1.14 is required.])])
])

# Look for libsodium, an optional dependency.
PKG_CHECK_MODULES([SODIUM], [libsodium], [CXXFLAGS="$SODIUM_CFLAGS $CXXFLAGS"])

# Look for libbrotli{enc,dec}.
PKG_CHECK_MODULES([LIBBROTLI], [libbrotlienc libbrotlidec], [CXXFLAGS="$LIBBROTLI_CFLAGS $CXXFLAGS"])

# Look for libcpuid.
if test "$machine_name" = "x86_64"; then
  PKG_CHECK_MODULES([LIBCPUID], [libcpuid], [CXXFLAGS="$LIBCPUID_CFLAGS $CXXFLAGS"])
  have_libcpuid=1
  AC_DEFINE([HAVE_LIBCPUID], [1], [Use libcpuid])
fi
AC_SUBST(HAVE_LIBCPUID, [$have_libcpuid])


# Look for libseccomp, required for Linux sandboxing.
case "$host_os" in
  linux*)
    AC_ARG_ENABLE([seccomp-sandboxing],
                  AS_HELP_STRING([--disable-seccomp-sandboxing],[Don't build support for seccomp sandboxing (only recommended if your arch doesn't support libseccomp yet!)
                                ]))
    if test "x$enable_seccomp_sandboxing" != "xno"; then
      PKG_CHECK_MODULES([LIBSECCOMP], [libseccomp],
                        [CXXFLAGS="$LIBSECCOMP_CFLAGS $CXXFLAGS"])
      have_seccomp=1
      AC_DEFINE([HAVE_SECCOMP], [1], [Whether seccomp is available and should be used for sandboxing.])
    else
      have_seccomp=
    fi
    ;;
  *)
    have_seccomp=
    ;;
esac
AC_SUBST(HAVE_SECCOMP, [$have_seccomp])


# Look for aws-cpp-sdk-s3.
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([aws/s3/S3Client.h],
  [AC_DEFINE([ENABLE_S3], [1], [Whether to enable S3 support via aws-sdk-cpp.]) enable_s3=1],
  [AC_DEFINE([ENABLE_S3], [0], [Whether to enable S3 support via aws-sdk-cpp.]) enable_s3=])
AC_SUBST(ENABLE_S3, [$enable_s3])
AC_LANG_POP(C++)

if test -n "$enable_s3"; then
  declare -a aws_version_tokens=($(printf '#include <aws/core/VersionConfig.h>\nAWS_SDK_VERSION_STRING' | $CPP $CPPFLAGS - | grep -v '^#.*' | sed 's/"//g' | tr '.' ' '))
  AC_DEFINE_UNQUOTED([AWS_VERSION_MAJOR], ${aws_version_tokens@<:@0@:>@}, [Major version of aws-sdk-cpp.])
  AC_DEFINE_UNQUOTED([AWS_VERSION_MINOR], ${aws_version_tokens@<:@1@:>@}, [Minor version of aws-sdk-cpp.])
  AC_DEFINE_UNQUOTED([AWS_VERSION_PATCH], ${aws_version_tokens@<:@2@:>@}, [Patch version of aws-sdk-cpp.])
fi


# Whether to use the Boehm garbage collector.
AC_ARG_ENABLE(gc, AS_HELP_STRING([--enable-gc],[enable garbage collection in the Nix expression evaluator (requires Boehm GC) [default=yes]]),
  gc=$enableval, gc=yes)
if test "$gc" = yes; then
  PKG_CHECK_MODULES([BDW_GC], [bdw-gc])
  CXXFLAGS="$BDW_GC_CFLAGS $CXXFLAGS"
  AC_DEFINE(HAVE_BOEHMGC, 1, [Whether to use the Boehm garbage collector.])
fi


# Look for gtest.
PKG_CHECK_MODULES([GTEST], [gtest_main])


# documentation generation switch
AC_ARG_ENABLE(doc-gen, AS_HELP_STRING([--disable-doc-gen],[disable documentation generation]),
  doc_generate=$enableval, doc_generate=yes)
AC_SUBST(doc_generate)


# Setuid installations.
AC_CHECK_FUNCS([setresuid setreuid lchown])


# Nice to have, but not essential.
AC_CHECK_FUNCS([strsignal posix_fallocate sysconf])


# This is needed if bzip2 is a static library, and the Nix libraries
# are dynamic.
case "${host_os}" in
  darwin*)
    LDFLAGS="-all_load $LDFLAGS"
    ;;
esac


AC_ARG_WITH(sandbox-shell, AS_HELP_STRING([--with-sandbox-shell=PATH],[path of a statically-linked shell to use as /bin/sh in sandboxes]),
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

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([])
AC_OUTPUT
