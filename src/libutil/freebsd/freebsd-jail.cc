#ifdef __FreeBSD__
#  include "nix/util/freebsd-jail.hh"

#  include <sys/resource.h>
#  include <sys/param.h>
#  include <sys/jail.h>
#  include <sys/mount.h>

#  include "nix/util/error.hh"
#  include "nix/util/util.hh"

namespace nix {

AutoRemoveJail::AutoRemoveJail() = default;

AutoRemoveJail::AutoRemoveJail(int jid)
    : jid(jid)
{
}

void AutoRemoveJail::remove()
{
    if (jid != INVALID_JAIL) {
        if (jail_remove(jid) < 0) {
            throw SysError("Failed to remove jail %1%", jid);
        }
    }
    cancel();
}

AutoRemoveJail::~AutoRemoveJail()
{
    try {
        remove();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void AutoRemoveJail::cancel()
{
    jid = INVALID_JAIL;
}

void AutoRemoveJail::reset(int j)
{
    jid = j;
}

//////////////////////////////////////////////////////////////////////

} // namespace nix
#endif
