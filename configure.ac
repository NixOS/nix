AC_INIT(nix, 0.1)
AC_CONFIG_SRCDIR(src/nix.cc)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT(/nix)

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_CXX

AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_FILES([src/nix-instantiate], [chmod +x src/nix-instantiate])
AC_OUTPUT