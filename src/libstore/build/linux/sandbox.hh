#pragma once

#if __linux__

#include "../local-derivation-goal.hh"
#include "util.hh"
#include "cgroup.hh"

#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#if HAVE_SECCOMP
#include <seccomp.h>
#endif
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))

#define REQUIRES_HASH_REWRITE false

namespace nix {
void doBind(const Path & source, const Path & target, bool optional = false);
void setupSeccomp();
}

#endif
