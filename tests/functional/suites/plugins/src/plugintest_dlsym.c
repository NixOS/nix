#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

static void require_symbol(void * handle, const char * symbol)
{
    // Clear any stale loader error. Without this, the dlerror() call below
    // could return the error from the previous call to dlsym() if the next
    // call to dlsym() returns a null pointer without setting an error.
    (void) dlerror();

    if (dlsym(handle, symbol))
        return;

    fprintf(stderr, "missing C API symbol %s", symbol);
    const char * error = dlerror();
    if (error)
        fprintf(stderr, ": %s", error);
    fprintf(stderr, "\n");
    abort();
}

void nix_plugin_entry()
{
    void * handle = dlopen(NULL, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "failed to access main executable symbols");
        const char * error = dlerror();
        if (error)
            fprintf(stderr, ": %s", error);
        fprintf(stderr, "\n");
        abort();
    }

    const char * symbols[] = {
        "nix_libutil_init",
        "nix_libstore_init",
        "nix_fetchers_settings_new",
        "nix_libexpr_init",
        "nix_flake_settings_new",
        "nix_init_plugins",
    };

    for (int i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        const char * symbol = symbols[i];
        require_symbol(handle, symbol);
    }
}
