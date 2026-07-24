/// Runtime versions of the third-parties libraries libstore adds on top of the
/// lower nix libraries' dependencies.

#include "nix/util/library-versions.hh"
#include "nix/util/fmt.hh"

#include "nix/store/config.hh"
#include "store-config-private.hh"

#include <curl/curl.h>
#include <sqlite3.h>

#if HAVE_SECCOMP
#  include <seccomp.h>
#endif

#if NIX_WITH_AWS_AUTH
#  include <aws/crt/Config.h>
#endif

namespace nix {

#if HAVE_SECCOMP
static std::string libseccompVersion()
{
    const auto * v = seccomp_version();
    return fmt("%d.%d.%d", v->major, v->minor, v->micro);
}
#endif

static RegisterLibraryVersion rLibcurl{
    "libcurl", [] { return std::string(curl_version_info(CURLVERSION_NOW)->version); }};

static RegisterLibraryVersion rLibsqlite3{"libsqlite3", [] { return std::string(sqlite3_libversion()); }};

#if HAVE_SECCOMP
static RegisterLibraryVersion rLibseccomp{"libseccomp", libseccompVersion};
#endif

#if NIX_WITH_AWS_AUTH
// Should be dynamic ideally, but avoid complicated aws-crt-cpp init for now.
static RegisterLibraryVersion rAwsCrtCpp{"aws-crt-cpp", [] { return std::string(AWS_CRT_CPP_VERSION); }};
#endif

} // namespace nix
