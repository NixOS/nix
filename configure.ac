AC_INIT(nix, 0.1)
AC_CONFIG_SRCDIR(src/nix.cc)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

# Unix shell scripting should die a slow and painful death.
AC_DEFINE_UNQUOTED(NIX_STORE_DIR, "$(eval echo $prefix/store)", Nix store directory.)
AC_DEFINE_UNQUOTED(NIX_STATE_DIR, "$(eval echo $localstatedir/nix)", Nix state directory.)
AC_DEFINE_UNQUOTED(NIX_LOG_DIR, "$(eval echo $localstatedir/log/nix)", Nix log file directory.)

AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile scripts/Makefile])
AC_OUTPUT
