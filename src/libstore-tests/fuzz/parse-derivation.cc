#include "nix/store/derivations.hh"
#include "nix/store/store-dir-config.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;
    std::string storeDir = "/nix/store";
    try {
        StoreDirConfig config{storeDir};
        parseDerivation(config, std::string(data, data + size), "test");
    } catch (const nix::Error &) {
    }
    return 0;
}
