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

        for (auto & path : fs::recursive_directory_iterator(nixDir)) {
            fs::permissions(path, fs::perms::owner_all);
        }
        fs::remove_all(nixDir);
    }

    Store * store;
    std::string nixDir;
    std::string nixStoreDir;

protected:
    void init_local_store()
    {
        auto tmpl = nix::getEnv("TMPDIR").value_or("/tmp") + "/tests_nix-store.XXXXXX";
        nixDir = mkdtemp((char *) tmpl.c_str());
        nixStoreDir = nixDir + "/my_nix_store";

        // Options documented in `nix help-stores`
        const char * p1[] = {"store", nixStoreDir.c_str()};
        const char * p2[] = {"state", (new std::string(nixDir + "/my_state"))->c_str()};
        const char * p3[] = {"log", (new std::string(nixDir + "/my_log"))->c_str()};

        const char ** params[] = {p1, p2, p3, nullptr};

        store = nix_store_open(ctx, "local", params);
    }
};
}
