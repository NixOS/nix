#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

/* We'd really like to avoid depending on the headers just for the test, however
   ASan in clang doesn't love when we use the underlying `int` type for the return
   type. So just define it here as a convenience while omitting some of the values. */
enum nix_err : int {
    NIX_OK = 0,
};

typedef enum nix_err nix_err;
typedef struct nix_c_context nix_c_context;

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

static bool entry_called = false;

void nix_plugin_entry(void)
{
    if (entry_called) {
        fprintf(stderr, "nix_plugin_entry called twice\n");
        fflush(stderr);
        abort();
    }

    entry_called = true;

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

    /* We don't need headers where we are going... */
    nix_err (*nix_init_plugins)(nix_c_context *) = (nix_err (*)(nix_c_context *)) dlsym(handle, "nix_init_plugins");
    nix_c_context * (*nix_c_context_create)(void) = (nix_c_context * (*) (void) ) dlsym(handle, "nix_c_context_create");

    if (!nix_c_context_create || !nix_init_plugins) {
        fprintf(stderr, "failed to look up nix_init_plugins or nix_c_context_create symbols\n");
        fflush(stderr);
        abort();
    }

    void * context = nix_c_context_create();
    if (!context) {
        fprintf(stderr, "failed create a new nix_c_context\n");
        fflush(stderr);
        abort();
    }

    /* This must fail. */
    int err = nix_init_plugins(context);
    if (err == NIX_OK) {
        fprintf(stderr, "nix_init_plugins should fail inside nix_plugin_entry\n");
        fflush(stderr);
        abort();
    }
}
