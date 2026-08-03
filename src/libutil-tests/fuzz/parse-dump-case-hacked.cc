#include "nix/util/config-global.hh"

#include "parse-dump-common.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;
    assert(globalConfig.set("use-case-hack", "true"));
    return fuzzParseDumpCommon(std::string_view(reinterpret_cast<const char *>(data), size), /*acceptInvalid=*/true);
}
