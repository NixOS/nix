#include "personality.hh"
#include "globals.hh"

#if __linux__
#include <sys/utsname.h>
#include <sys/personality.h>
#endif

#include <cstring>

namespace nix {

void setPersonality(std::string_view system)
{
#if __linux__
        /* Change the personality to 32-bit if we're doing an
           i686-linux build on an x86_64-linux machine. */
        struct utsname utsbuf;
        uname(&utsbuf);
        if ((system == "i686-linux"
                && (std::string_view(SYSTEM) == "x86_64-linux"
                    || (!strcmp(utsbuf.sysname, "Linux") && !strcmp(utsbuf.machine, "x86_64"))))
            || system == "armv7l-linux"
            || system == "armv6l-linux"
            || system == "armv5tel-linux")
        {
            if (personality(PER_LINUX32) == -1)
                throw SysError("cannot set 32-bit personality");
        }

        /* Impersonate a Linux 2.6 machine to get some determinism in
           builds that depend on the kernel version. */
        if ((system == "i686-linux" || system == "x86_64-linux") && settings.impersonateLinux26) {
            int cur = personality(0xffffffff);
            if (cur != -1) personality(cur | 0x0020000 /* == UNAME26 */);
        }

        /* Disable address space randomization for improved
           determinism. */
        int cur = personality(0xffffffff);
        if (cur != -1) personality(cur | ADDR_NO_RANDOMIZE);
#endif
}

}
