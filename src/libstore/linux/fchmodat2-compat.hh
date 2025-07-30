#include "store-config-private.hh"
#include <cstdint>

/*
 * Determine the syscall number for `fchmodat2`.
 *
 * On most platforms this is 452. Exceptions can be found on
 * a glibc git checkout via `rg --pcre2 'define __NR_fchmodat2 (?!452)'`.
 *
 * The problem is that glibc 2.39 and libseccomp 2.5.5 are needed to
 * get the syscall number. However, a Nix built against nixpkgs 23.11
 * (glibc 2.38) should still have the issue fixed without depending
 * on the build environment.
 *
 * To achieve that, the macros below try to determine the platform and
 * set the syscall number which is platform-specific, but
 * in most cases 452.
 *
 * TODO: remove this when 23.11 is EOL and the entire (supported) ecosystem
 * is on glibc 2.39.
 */

#if HAVE_SECCOMP
#  if defined(__alpha__)
#    define NIX_SYSCALL_FCHMODAT2 562
#  elif defined(__x86_64__) && SIZE_MAX == 0xFFFFFFFF // x32
#    define NIX_SYSCALL_FCHMODAT2 1073742276
#  elif defined(__mips__) && defined(__mips64) && defined(_ABIN64) // mips64/n64
#    define NIX_SYSCALL_FCHMODAT2 5452
#  elif defined(__mips__) && defined(__mips64) && defined(_ABIN32) // mips64/n32
#    define NIX_SYSCALL_FCHMODAT2 6452
#  elif defined(__mips__) && defined(_ABIO32) // mips32
#    define NIX_SYSCALL_FCHMODAT2 4452
#  else
#    define NIX_SYSCALL_FCHMODAT2 452
#  endif
#endif // HAVE_SECCOMP
