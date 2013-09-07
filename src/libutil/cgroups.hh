#pragma once

#include "types.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <cstdio>


namespace nix {

/* group handling functions */

Cgroups getCgroups(long pid = -1);
void joinCgroups(const Cgroups &);

}
