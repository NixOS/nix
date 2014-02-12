makefiles = \
  local.mk \
  src/boost/format/local.mk \
  src/libutil/local.mk \
  src/libstore/local.mk \
  src/libmain/local.mk \
  src/libexpr/local.mk \
  src/nix-hash/local.mk \
  src/nix-store/local.mk \
  src/nix-instantiate/local.mk \
  src/nix-env/local.mk \
  src/nix-daemon/local.mk \
  src/download-via-ssh/local.mk \
  src/nix-log2xml/local.mk \
  src/bsdiff-4.3/local.mk \
  perl/local.mk \
  scripts/local.mk \
  corepkgs/local.mk \
  misc/emacs/local.mk \
  doc/manual/local.mk \
  tests/local.mk

include Makefile.config

include mk/lib.mk
