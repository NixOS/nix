#pragma once
///@file
#include "nix/util/tests/nix_api_util.hh"

#include "nix/util/file-system.hh"
#include <filesystem>

#include "nix_api_store.h"
#include "nix_api_store_internal.h"

#include <filesystem>
#include <gtest/gtest.h>

namespace nixC {

class nix_api_store_test_base : public nix_api_util_context
{
public:
    nix_api_store_test_base()
    {
        nix_libstore_init(ctx);
    };

    ~nix_api_store_test_base() override
    {
        if (exists(std::filesystem::path{nixDir})) {
            for (auto & path : std::filesystem::recursive_directory_iterator(nixDir)) {
                std::filesystem::permissions(path, std::filesystem::perms::owner_all);
            }
            std::filesystem::remove_all(nixDir);
        }
    }

    std::string nixDir;
    std::string nixStoreDir;
    std::string nixStateDir;
    std::string nixLogDir;

protected:
    Store * open_local_store()
    {
#ifdef _WIN32
        // no `mkdtemp` with MinGW
        auto tmpl = nix::defaultTempDir() + "/tests_nix-store.";
        for (size_t i = 0; true; ++i) {
            nixDir = tmpl + std::string{i};
            if (std::filesystem::create_directory(nixDir))
                break;
        }
#else
        // resolve any symlinks in i.e. on macOS /tmp -> /private/tmp
        // because this is not allowed for a nix store.
        auto tmpl = nix::absPath(std::filesystem::path(nix::defaultTempDir()) / "tests_nix-store.XXXXXX", true);
        nixDir = mkdtemp((char *) tmpl.c_str());
#endif

        nixStoreDir = nixDir + "/my_nix_store";
        nixStateDir = nixDir + "/my_state";
        nixLogDir = nixDir + "/my_log";

        // Options documented in `nix help-stores`
        const char * p1[] = {"store", nixStoreDir.c_str()};
        const char * p2[] = {"state", nixStateDir.c_str()};
        const char * p3[] = {"log", nixLogDir.c_str()};

        const char ** params[] = {p1, p2, p3, nullptr};

        auto * store = nix_store_open(ctx, "local", params);
        if (!store) {
            std::string errMsg = nix_err_msg(nullptr, ctx, nullptr);
            EXPECT_NE(store, nullptr) << "Could not open store: " << errMsg;
            assert(store);
        };
        return store;
    }
};

class nix_api_store_test : public nix_api_store_test_base
{
public:
    nix_api_store_test()
        : nix_api_store_test_base{}
    {
        init_local_store();
    };

    ~nix_api_store_test() override
    {
        nix_store_free(store);
    }

    Store * store;

protected:
    void init_local_store()
    {
        store = open_local_store();
    }
};

} // namespace nixC
