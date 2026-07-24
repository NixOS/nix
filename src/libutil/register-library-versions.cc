/// Runtime versions of the third-parties libraries added by libutil.

#include "nix/util/library-versions.hh"
#include "nix/util/fmt.hh"

#include "util-config-private.hh"

#include <blake3.h>
#include <openssl/crypto.h>
#include <archive.h>
#include <sodium/version.h>
#include <brotli/encode.h>
#include <zstd.h>
#include <boost/version.hpp>
#include <nlohmann/json.hpp>

#if HAVE_LIBCPUID
#  include <libcpuid/libcpuid.h>
#endif

namespace nix {

static std::string libarchiveVersion()
{
    // archive_version_string() returns e.g. "libarchive 3.8.7"; keep only the number.
    std::string s = archive_version_string();
    auto pos = s.rfind(' ');
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

static std::string libbrotliVersion()
{
    uint32_t v = BrotliEncoderVersion();
    return fmt("%d.%d.%d", v >> 24, (v >> 12) & 0xfff, v & 0xfff);
}

static std::string boostVersion()
{
    // This is the documented method
    return fmt("%d.%d.%d", BOOST_VERSION / 100000, (BOOST_VERSION / 100) % 1000, BOOST_VERSION % 100);
}

static std::string nlohmannJsonVersion()
{
    return fmt("%d.%d.%d", NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH);
}

static RegisterLibraryVersion rLibblake3{"libblake3", [] { return std::string(blake3_version()); }};

static RegisterLibraryVersion rLibcrypto{
    "libcrypto", [] { return std::string(OpenSSL_version(OPENSSL_VERSION_STRING)); }};

static RegisterLibraryVersion rLibarchive{"libarchive", libarchiveVersion};

static RegisterLibraryVersion rLibsodium{"libsodium", [] { return std::string(sodium_version_string()); }};

static RegisterLibraryVersion rLibbrotli{"libbrotli", libbrotliVersion};

static RegisterLibraryVersion rLibzstd{"libzstd", [] { return std::string(ZSTD_versionString()); }};

// Header-only and compiled-in dependencies with no runtime version query; the
// compile-time version is the one Nix was built with.
static RegisterLibraryVersion rBoost{"boost", boostVersion};

static RegisterLibraryVersion rNlohmannJson{"nlohmann_json", nlohmannJsonVersion};

#if HAVE_LIBCPUID
static RegisterLibraryVersion rLibcpuid{"libcpuid", [] { return std::string(cpuid_lib_version()); }};
#endif

} // namespace nix
