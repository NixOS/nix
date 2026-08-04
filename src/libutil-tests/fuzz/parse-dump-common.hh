#include "nix/util/archive.hh"

namespace nix {

int fuzzParseDumpCommon(std::string_view s, bool acceptInvalid)
{
    nix::StringSource src{s};

    NullFileSystemObjectSink sink;
    try {
        nix::parseDump(sink, src);
    } catch (const nix::Error &) {
        return acceptInvalid ? 0 : -1;
    }

    /* TODO: Also check that it round-trips with dumpPath. Need to work around
       overly large allocation issues with MemorySourceAccessor probably. Also
       fuzz with more sink types. */
    return 0;
}

} // namespace nix
