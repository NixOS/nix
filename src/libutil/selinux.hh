#pragma once

#include "logging.hh"
#include "types.hh"

#include <functional>
#include <errno.h>
#include <sys/stat.h>

#if HAVE_SELINUX
#include <selinux/label.h>
#endif

namespace nix {

extern Logger * logger;

#if HAVE_SELINUX
static bool isSELinuxEnabled = is_selinux_enabled() > 0;
#endif

class SELinux
{
protected:
#if HAVE_SELINUX
    struct selabel_handle * labelHandle = NULL;
#endif

public:
    SELinux();
    ~SELinux();

    template<typename T>
    T withContext(const std::string & path, mode_t mode, std::function<T(const std::string &)> f)
    {
#if HAVE_SELINUX
        char * context = NULL;

        if (!labelHandle)
            return f(path);

        int r = selabel_lookup_raw(labelHandle, &context, path.c_str(), mode);
        if (r < 0) {
            if (errno != ENOENT)
                logger->log(lvlError, fmt("error determining SELinux context of %s", path));
        }
        else {
            logger->log(lvlDebug, fmt("setting SELinux context of %s to %s", path, context));
            if (setfscreatecon_raw(context) < 0)
                logger->log(lvlError, fmt("error setting SELinux context for %s to %s", path, context));
        }
#endif

        T result = f(path);

#if HAVE_SELINUX
        if (context)
            freecon(context);
        setfscreatecon_raw(NULL);
#endif

        return result;
    }

    template<typename T>
    T withFileContext(const std::string & path, std::function<T(const std::string &)> f)
    {
        return withContext<T>(path, S_IFREG, f);
    }

    template<typename T>
    T withDirectoryContext(const std::string & path, std::function<T(const std::string &)> f)
    {
        return withContext<T>(path, S_IFDIR, f);
    }

    template<typename T>
    T withLinkContext(const std::string & path, std::function<T(const std::string &)> f)
    {
        return withContext<T>(path, S_IFLNK, f);
    }
};

}
