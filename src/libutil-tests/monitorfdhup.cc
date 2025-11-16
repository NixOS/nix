// TODO: investigate why this is hanging on cygwin
#if !defined(_WIN32) && !defined(__CYGWIN__)

#  include "nix/util/util.hh"
#  include "nix/util/monitor-fd.hh"

#  include <sys/file.h>
#  include <gtest/gtest.h>

namespace nix {
TEST(MonitorFdHup, shouldNotBlock)
{
    Pipe p;
    p.create();
    {
        // when monitor gets destroyed it should cancel the
        // background thread and do not block
        MonitorFdHup monitor(p.readSide.get());
    }
}
} // namespace nix

#endif
