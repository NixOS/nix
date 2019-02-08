#include "selinux.hh"

#include <iostream>
#include <cstdlib>

#if HAVE_SELINUX
#include <selinux/context.h>
#include <selinux/label.h>
#include <selinux/selinux.h>
#endif

namespace nix {

SELinux::SELinux()
{
#if HAVE_SELINUX
    if (isSELinuxEnabled) {
        labelHandle = selabel_open(SELABEL_CTX_FILE, NULL, 0);

        if (!labelHandle)
            logger->log(lvlError, "failed to initialize SELinux file context");
    }
#endif
}

SELinux::~SELinux()
{
#if HAVE_SELINUX
    if (labelHandle)
        selabel_close(labelHandle);
#endif
}

}
