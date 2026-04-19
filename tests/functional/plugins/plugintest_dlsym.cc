#ifndef _WIN32
#  include <dlfcn.h>
#endif

#include <stdexcept>
#include <string>

namespace {

void requireSymbol(void * handle, const char * symbol)
{
    dlerror();
    if (dlsym(handle, symbol))
        return;

    std::string message = "missing C API symbol " + std::string(symbol);
    if (auto * error = dlerror())
        message += ": " + std::string(error);
    throw std::runtime_error(message);
}

} // namespace

extern "C" void nix_plugin_entry()
{
#ifndef _WIN32
    void * handle = dlopen(nullptr, RTLD_LAZY);
    if (!handle) {
        std::string message = "failed to access main executable symbols";
        if (auto * error = dlerror())
            message += ": " + std::string(error);
        throw std::runtime_error(message);
    }

    const char * symbols[] = {
        "nix_libutil_init",
        "nix_libstore_init",
        "nix_fetchers_settings_new",
        "nix_libexpr_init",
        "nix_flake_settings_new",
        "nix_init_plugins",
    };

    for (auto * symbol : symbols) {
        requireSymbol(handle, symbol);
    }
#endif
}
