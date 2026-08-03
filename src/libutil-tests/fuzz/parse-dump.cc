#include "parse-dump-common.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;
    return fuzzParseDumpCommon(std::string_view(reinterpret_cast<const char *>(data), size), /*acceptInvalid=*/true);
}
