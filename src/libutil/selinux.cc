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

void SELinux::restoreContext(const std::string & path, mode_t mode)
{
#if HAVE_SELINUX
        char * context = NULL;

        if (!labelHandle)
            return;

        int r = selabel_lookup_raw(labelHandle, &context, path.c_str(), mode);
        if (r < 0) {
            if (errno != ENOENT)
                logger->log(lvlError, fmt("error determining SELinux context of %s", path));
        }
        else {
            logger->log(lvlDebug, fmt("setting SELinux context of %s to %s", path, context));
            if (setfilecon_raw(path.c_str(), context) < 0)
                logger->log(lvlError, fmt("error setting SELinux context of %s to %s", path, context));
        }

        if (context)
            freecon(context);
#endif
}

}
