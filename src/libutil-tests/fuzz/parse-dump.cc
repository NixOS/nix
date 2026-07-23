#include "nix/util/archive.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;
    nix::StringSource src{std::string_view(reinterpret_cast<const char *>(data), size)};
    NullFileSystemObjectSink sink;
    try {
        nix::parseDump(sink, src);
    } catch (const nix::Error &) {
    }
    return 0;
}
