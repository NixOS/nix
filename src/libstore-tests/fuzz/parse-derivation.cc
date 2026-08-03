#include "parse-derivation-common.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;

    return fuzzParseDerivationCommon(
        std::string_view(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(data) + size));
}
