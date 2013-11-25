SUBS = \
  src/boost/format/Makefile \
  src/libutil/Makefile \
  src/libstore/Makefile \
  src/libmain/Makefile \
  src/libexpr/Makefile \
  src/nix-hash/Makefile \
  src/nix-store/Makefile \
  src/nix-instantiate/Makefile \
  src/nix-env/Makefile \
  src/nix-daemon/Makefile \
  src/nix-log2xml/Makefile \
  src/bsdiff-4.3/Makefile \
  perl/Makefile \
  scripts/Makefile \
  corepkgs/Makefile

GLOBAL_CXXFLAGS = -I . -I src -I src/libutil -I src/libstore -I src/libmain -I src/libexpr

include mk/lib.mk
