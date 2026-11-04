#include "nix/util/terminal.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;

    std::string_view s(reinterpret_cast<const char *>(data), size);

    auto stripped = stripANSIEscapes(s);
    // Idempotency.
    if (stripped != stripANSIEscapes(stripped))
        __builtin_trap();

    // Just don't crash.
    for (bool filterAll : {false, true})
        for (unsigned width : {9, 80})
            filterANSIEscapes(s, /*filterAll=*/filterAll, /*width=*/width);

    return 0;
}
