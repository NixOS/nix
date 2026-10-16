#include "nix/store/personality.hh"
#include "nix/store/config.hh"
#include "nix/util/error.hh"

#include <sys/utsname.h>
#include <sys/personality.h>

#include <cstring>

namespace nix::linux {

void setPersonality(PersonalityArgs args)
{
    /* Change the personality to 32-bit if we're doing an
       i686-linux build on an x86_64-linux machine. */
    struct utsname utsbuf;
    uname(&utsbuf);
    if ((args.system == "i686-linux"
         && (std::string_view(NIX_LOCAL_SYSTEM) == "x86_64-linux"
             || (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64"))))
        || args.system == "armv7l-linux" || args.system == "armv6l-linux" || args.system == "armv5tel-linux") {
        if (personality(PER_LINUX32) == -1)
            throw SysError("cannot set 32-bit personality");
    }

    /* Disable address space randomization for improved
       determinism. */
    if (args.disableASLR) {
        int cur = personality(0xffffffff);
        if (cur != -1)
            personality(cur | ADDR_NO_RANDOMIZE);
    }
}

} // namespace nix::linux
