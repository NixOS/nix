#pragma once
///@file
#include "nix/util/tests/nix_api_util.hh"

#include "nix/util/file-system.hh"
#include <filesystem>

#include "nix_api_store.h"
#include "nix_api_store_internal.h"

#include <filesystem>
#include <gtest/gtest.h>

namespace fs {
using namespace std::filesystem;
}

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
#ifdef _WIN32
        // no `mkdtemp` with MinGW
        auto tmpl = nix::defaultTempDir() + "/tests_nix-store.";
        for (size_t i = 0; true; ++i) {
            nixDir = tmpl + std::string{i};
            if (fs::create_directory(nixDir))
                break;
        }
#else
        // resolve any symlinks in i.e. on macOS /tmp -> /private/tmp
        // because this is not allowed for a nix store.
        auto tmpl = nix::absPath(std::filesystem::path(nix::defaultTempDir()) / "tests_nix-store.XXXXXX", true);
        nixDir = mkdtemp((char *) tmpl.c_str());
#endif

        nixStoreDir = nixDir + "/my_nix_store";

        // Options documented in `nix help-stores`
        const char * p1[] = {"store", nixStoreDir.c_str()};
        const char * p2[] = {"state", (new std::string(nixDir + "/my_state"))->c_str()};
        const char * p3[] = {"log", (new std::string(nixDir + "/my_log"))->c_str()};

        const char ** params[] = {p1, p2, p3, nullptr};

        store = nix_store_open(ctx, "local", params);
        if (!store) {
            std::string errMsg = nix_err_msg(nullptr, ctx, nullptr);
            ASSERT_NE(store, nullptr) << "Could not open store: " << errMsg;
        };
    }
};
}
