#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

namespace {

void requireSymbol(void * handle, const char * symbol)
{
    // Clear any stale loader error. Without this, the dlerror() call below
    // could return the error from the previous call to dlsym() if the next
    // call to dlsym() returns a null pointer without setting an error.
    (void) dlerror();

    if (dlsym(handle, symbol))
        return;

    std::fprintf(stderr, "missing C API symbol %s", symbol);
    if (auto * error = dlerror())
        std::fprintf(stderr, ": %s", error);
    std::fprintf(stderr, "\n");
    std::abort();
}

} // namespace

extern "C" void nix_plugin_entry()
{
    void * handle = dlopen(nullptr, RTLD_LAZY);
    if (!handle) {
        std::fprintf(stderr, "failed to access main executable symbols");
        if (auto * error = dlerror())
            std::fprintf(stderr, ": %s", error);
        std::fprintf(stderr, "\n");
        std::abort();
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
}
