AC_INIT([nix],[m4_esyscmd(bash -c "echo -n $(cat ./.version)$VERSION_SUFFIX")])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR(README.md)
AC_CONFIG_AUX_DIR(config)

AC_PROG_SED

# Construct a Nix system name (like "i686-linux"):
# https://www.gnu.org/software/autoconf/manual/html_node/Canonicalizing.html#index-AC_005fCANONICAL_005fHOST-1
# The inital value is produced by the `config/config.guess` script:
# upstream: https://git.savannah.gnu.org/cgit/config.git/tree/config.guess
# It has the following form, which is not documented anywhere:
# <cpu>-<vendor>-<os>[<version>][-<abi>]
# If `./configure` is passed any of the `--host`, `--build`, `--target` options, the value comes from `config/config.sub` instead:
# upstream: https://git.savannah.gnu.org/cgit/config.git/tree/config.sub
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

# Assign a default value to C{,XX}FLAGS as the default configure script sets them
# to -O2 otherwise, which we don't want to have hardcoded
CFLAGS=${CFLAGS-""}
CXXFLAGS=${CXXFLAGS-""}

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


ENSURE_NO_GCC_BUG_80431


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


AC_SUBST(coreutils, [$(dirname $(type -p cat))])


AC_ARG_WITH(store-dir, AS_HELP_STRING([--with-store-dir=PATH],[path of the Nix store (defaults to /nix/store)]),
  storedir=$withval, storedir='/nix/store')
AC_SUBST(storedir)


# Running the functional tests without building Nix is useful for testing
# different pre-built versions of Nix against each other.
AC_ARG_ENABLE(build, AS_HELP_STRING([--disable-build],[Do not build nix]),
  ENABLE_BUILD=$enableval, ENABLE_BUILD=yes)
AC_SUBST(ENABLE_BUILD)

# Building without unit tests is useful for bootstrapping with a smaller footprint
# or running the tests in a separate derivation. Otherwise, we do compile and
# run them.

AC_ARG_ENABLE(unit-tests, AS_HELP_STRING([--disable-unit-tests],[Do not build the tests]),
  ENABLE_UNIT_TESTS=$enableval, ENABLE_UNIT_TESTS=$ENABLE_BUILD)
AC_SUBST(ENABLE_UNIT_TESTS)

# Build external API docs by default
AC_ARG_ENABLE(external_api_docs, AS_HELP_STRING([--enable-external-api-docs],[Build API docs for Nix's C interface]),
  external_api_docs=$enableval, external_api_docs=yes)
AC_SUBST(external_api_docs)

AS_IF(
  [test "$ENABLE_BUILD" == "no" && test "$ENABLE_UNIT_TESTS" == "yes"],
  [AC_MSG_ERROR([Cannot enable unit tests when building overall is disabled. Please do not pass '--enable-unit-tests' or do not pass '--disable-build'.])])

AC_ARG_ENABLE(functional-tests, AS_HELP_STRING([--disable-functional-tests],[Do not build the tests]),
  ENABLE_FUNCTIONAL_TESTS=$enableval, ENABLE_FUNCTIONAL_TESTS=yes)
AC_SUBST(ENABLE_FUNCTIONAL_TESTS)

# documentation generation switch
AC_ARG_ENABLE(doc-gen, AS_HELP_STRING([--disable-doc-gen],[disable documentation generation]),
  ENABLE_DOC_GEN=$enableval, ENABLE_DOC_GEN=$ENABLE_BUILD)
AC_SUBST(ENABLE_DOC_GEN)

AS_IF(
  [test "$ENABLE_BUILD" == "no" && test "$ENABLE_DOC_GEN" == "yes"],
  [AC_MSG_ERROR([Cannot enable generated docs when building overall is disabled. Please do not pass '--enable-doc-gen' or do not pass '--disable-build'.])])

# Building without API docs is the default as Nix' C++ interfaces are internal and unstable.
AC_ARG_ENABLE(internal-api-docs, AS_HELP_STRING([--enable-internal-api-docs],[Build API docs for Nix's internal unstable C++ interfaces]),
  ENABLE_INTERNAL_API_DOCS=$enableval, ENABLE_INTERNAL_API_DOCS=no)
AC_SUBST(ENABLE_INTERNAL_API_DOCS)

AC_ARG_ENABLE(external-api-docs, AS_HELP_STRING([--enable-external-api-docs],[Build API docs for Nix's external unstable C interfaces]),
  ENABLE_EXTERNAL_API_DOCS=$enableval, ENABLE_EXTERNAL_API_DOCS=no)
AC_SUBST(ENABLE_EXTERNAL_API_DOCS)

AS_IF(
  [test "$ENABLE_FUNCTIONAL_TESTS" == "yes" || test "$ENABLE_DOC_GEN" == "yes"],
  [NEED_PROG(jq, jq)])

AS_IF([test "$ENABLE_BUILD" == "yes"],[

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

AC_ARG_ENABLE(install-unit-tests, AS_HELP_STRING([--enable-install-unit-tests],[Install the unit tests for running later (default no)]),
  INSTALL_UNIT_TESTS=$enableval, INSTALL_UNIT_TESTS=no)
AC_SUBST(INSTALL_UNIT_TESTS)

AC_ARG_WITH(check-bin-dir, AS_HELP_STRING([--with-check-bin-dir=PATH],[path to install unit tests for running later (defaults to $libexecdir/nix)]),
  checkbindir=$withval, checkbindir=$libexecdir/nix)
AC_SUBST(checkbindir)

AC_ARG_WITH(check-lib-dir, AS_HELP_STRING([--with-check-lib-dir=PATH],[path to install unit tests for running later (defaults to $libdir)]),
  checklibdir=$withval, checklibdir=$libdir)
AC_SUBST(checklibdir)

# LTO is currently broken with clang for unknown reasons; ld segfaults in the llvm plugin
AC_ARG_ENABLE(lto, AS_HELP_STRING([--enable-lto],[Enable LTO (only supported with GCC) [default=no]]),
  lto=$enableval, lto=no)
if test "$lto" = yes; then
    if $CXX --version | grep -q GCC; then
        AC_SUBST(CXXLTO, [-flto=jobserver])
    else
        echo "error: LTO is only supported with GCC at the moment" >&2
        exit 1
    fi
else
    AC_SUBST(CXXLTO, [""])
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
PKG_CHECK_MODULES([OPENSSL], [libcrypto >= 1.1.1], [CXXFLAGS="$OPENSSL_CFLAGS $CXXFLAGS"])


# Look for libarchive.
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive >= 3.1.2], [CXXFLAGS="$LIBARCHIVE_CFLAGS $CXXFLAGS"])
# Workaround until https://github.com/libarchive/libarchive/issues/1446 is fixed
if test "$shared" != yes; then
    LIBARCHIVE_LIBS+=' -lz'
fi

# Look for SQLite, a required dependency.
PKG_CHECK_MODULES([SQLITE3], [sqlite3 >= 3.6.19], [CXXFLAGS="$SQLITE3_CFLAGS $CXXFLAGS"])

# Look for libcurl, a required dependency.
PKG_CHECK_MODULES([LIBCURL], [libcurl], [CXXFLAGS="$LIBCURL_CFLAGS $CXXFLAGS"])

# Look for editline or readline, a required dependency.
# The the libeditline.pc file was added only in libeditline >= 1.15.2,
# see https://github.com/troglobit/editline/commit/0a8f2ef4203c3a4a4726b9dd1336869cd0da8607,
# Older versions are no longer supported.
AC_ARG_WITH(
  [readline-flavor],
  AS_HELP_STRING([--with-readline-flavor],[Which library to use for nice line editting with the Nix language REPL" [default=editline]]),
  [readline_flavor=$withval],
  [readline_flavor=editline])
AS_CASE(["$readline_flavor"],
  [editline], [
    readline_flavor_pc=libeditline
  ],
  [readline], [
    readline_flavor_pc=readline
    AC_DEFINE([USE_READLINE], [1], [Use readline instead of editline])
  ],
  [AC_MSG_ERROR([bad value "$readline_flavor" for --with-readline-flavor, must be one of: editline, readline])])
PKG_CHECK_MODULES([EDITLINE], [$readline_flavor_pc], [CXXFLAGS="$EDITLINE_CFLAGS $CXXFLAGS"])

# Look for libsodium.
PKG_CHECK_MODULES([SODIUM], [libsodium], [CXXFLAGS="$SODIUM_CFLAGS $CXXFLAGS"])

# Look for libbrotli{enc,dec}.
PKG_CHECK_MODULES([LIBBROTLI], [libbrotlienc libbrotlidec], [CXXFLAGS="$LIBBROTLI_CFLAGS $CXXFLAGS"])

# Look for libcpuid.
have_libcpuid=
if test "$machine_name" = "x86_64"; then
    AC_ARG_ENABLE([cpuid],
                  AS_HELP_STRING([--disable-cpuid], [Do not determine microarchitecture levels with libcpuid (relevant to x86_64 only)]))
    if test "x$enable_cpuid" != "xno"; then
      PKG_CHECK_MODULES([LIBCPUID], [libcpuid],
        [CXXFLAGS="$LIBCPUID_CFLAGS $CXXFLAGS"
         have_libcpuid=1
         AC_DEFINE([HAVE_LIBCPUID], [1], [Use libcpuid])]
      )
    fi
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

# Optional dependencies for better normalizing file system data
AC_CHECK_HEADERS([sys/xattr.h])
AS_IF([test "$ac_cv_header_sys_xattr_h" = "yes"],[
  AC_CHECK_FUNCS([llistxattr lremovexattr])
  AS_IF([test "$ac_cv_func_llistxattr" = "yes" && test "$ac_cv_func_lremovexattr" = "yes"],[
    AC_DEFINE([HAVE_ACL_SUPPORT], [1], [Define if we can manipulate file system Access Control Lists])
  ])
])

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

AS_IF([test "$ENABLE_UNIT_TESTS" == "yes"],[

# Look for gtest.
PKG_CHECK_MODULES([GTEST], [gtest_main gmock_main])

# Look for rapidcheck.
PKG_CHECK_MODULES([RAPIDCHECK], [rapidcheck rapidcheck_gtest])

])

# Look for nlohmann/json.
PKG_CHECK_MODULES([NLOHMANN_JSON], [nlohmann_json >= 3.9])


# Look for lowdown library.
AC_ARG_ENABLE([markdown], AS_HELP_STRING([--enable-markdown], [Enable Markdown rendering in the Nix binary (requires lowdown) [default=auto]]),
  enable_markdown=$enableval, enable_markdown=auto)
AS_CASE(["$enable_markdown"],
  [yes | auto], [
    PKG_CHECK_MODULES([LOWDOWN], [lowdown >= 0.9.0], [
      CXXFLAGS="$LOWDOWN_CFLAGS $CXXFLAGS"
      have_lowdown=1
      AC_DEFINE(HAVE_LOWDOWN, 1, [Whether lowdown is available and should be used for Markdown rendering.])
    ], [
      AS_IF([test "x$enable_markdown" == "xyes"], [AC_MSG_ERROR([--enable-markdown was specified, but lowdown was not found.])])
    ])
  ],
  [no], [have_lowdown=],
  [AC_MSG_ERROR([bad value "$enable_markdown" for --enable-markdown, must be one of: yes, no, auto])])


# Look for libgit2.
PKG_CHECK_MODULES([LIBGIT2], [libgit2])


# Setuid installations.
AC_CHECK_FUNCS([setresuid setreuid lchown])


# Nice to have, but not essential.
AC_CHECK_FUNCS([strsignal posix_fallocate sysconf])


AC_ARG_WITH(sandbox-shell, AS_HELP_STRING([--with-sandbox-shell=PATH],[path of a statically-linked shell to use as /bin/sh in sandboxes]),
  sandbox_shell=$withval)
AC_SUBST(sandbox_shell)
if test ${cross_compiling:-no} = no && ! test -z ${sandbox_shell+x}; then
  AC_MSG_CHECKING([whether sandbox-shell has the standalone feature])
  # busybox shell sometimes allows executing other busybox applets,
  # even if they are not in the path, breaking our sandbox
  if PATH= $sandbox_shell -c "busybox" 2>&1 | grep -qv "not found"; then
    AC_MSG_RESULT(enabled)
    AC_MSG_ERROR([Please disable busybox FEATURE_SH_STANDALONE])
  else
    AC_MSG_RESULT(disabled)
  fi
fi

AC_ARG_ENABLE(embedded-sandbox-shell, AS_HELP_STRING([--enable-embedded-sandbox-shell],[include the sandbox shell in the Nix binary [default=no]]),
  embedded_sandbox_shell=$enableval, embedded_sandbox_shell=no)
AC_SUBST(embedded_sandbox_shell)
if test "$embedded_sandbox_shell" = yes; then
  AC_DEFINE(HAVE_EMBEDDED_SANDBOX_SHELL, 1, [Include the sandbox shell in the Nix binary.])
fi

])


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
