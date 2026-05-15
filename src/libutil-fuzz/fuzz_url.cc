/*
 * libFuzzer harness for `nix::parseURL`.
 *
 * Feeds the input bytes verbatim to the URL parser. `nix::BadURL`
 * (derived from `nix::Error`) is caught as the expected outcome for
 * malformed input; anything else propagates so libFuzzer / the
 * sanitizers can report it.
 */

#include "nix/util/error.hh"
#include "nix/util/url.hh"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

// Inputs much larger than this don't tend to surface new parser bugs and
// just slow the mutator down.
constexpr std::size_t kMaxInputSize = 64 * 1024;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    if (size > kMaxInputSize)
        return -1;

    std::string_view input{reinterpret_cast<const char *>(data), size};

    try {
        (void) nix::parseURL(input);
    } catch (const nix::Error &) {
        // Malformed URLs throw nix::BadURL (a nix::Error).
    }

    return 0;
}
