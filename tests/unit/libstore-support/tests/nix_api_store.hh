#pragma once
///@file
#include "tests/nix_api_util.hh"

#include "nix_api_store.h"
#include "nix_api_store_internal.h"

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace nixC {
class nix_api_store_test : public nix_api_util_context
{
public:
    nix_api_store_test()
    {
        nix_libstore_init(ctx);
        init_local_store();
    };

    ~nix_api_store_test() override
    {
        nix_store_free(store);

        for (auto & path : fs::recursive_directory_iterator(nixStoreDir)) {
            fs::permissions(path, fs::perms::owner_all);
        }
        fs::remove_all(nixStoreDir);
    }

    Store * store;
    std::string nixStoreDir;

protected:
    void init_local_store()
    {
        auto tmpl = nix::getEnv("TMPDIR").value_or("/tmp") + "/tests_nix-store.XXXXXX";
        nixStoreDir = mkdtemp((char *) tmpl.c_str());

        // Options documented in `nix help-stores`
        const char * p1[] = {"root", nixStoreDir.c_str()};
        const char ** params[] = {p1, nullptr};
        store = nix_store_open(ctx, "local", params);
    }
};
}
