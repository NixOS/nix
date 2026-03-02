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
        std::error_code ec;
        if (exists(std::filesystem::path{nixDir}, ec)) {
            // Best-effort cleanup - ignore errors since we're in a destructor
            try {
                for (auto & path : std::filesystem::recursive_directory_iterator(
                         nixDir, std::filesystem::directory_options::skip_permission_denied)) {
                    std::filesystem::permissions(path, std::filesystem::perms::owner_all, ec);
                }
            } catch (...) {
                // Ignore iteration errors (broken symlinks, etc.)
            }
            std::filesystem::remove_all(nixDir, ec);
        }
    }

    std::filesystem::path nixDir;
    std::filesystem::path nixStoreDir;
    std::filesystem::path nixStateDir;
    std::filesystem::path nixLogDir;

protected:
    Store * open_local_store()
    {
#ifdef _WIN32
        // no `mkdtemp` with MinGW
        auto tmpl = nix::defaultTempDir() / "tests_nix-store.";
        for (size_t i = 0; true; ++i) {
            nixDir = tmpl.string() + std::to_string(i);
            if (std::filesystem::create_directory(nixDir))
                break;
        }
#else
        // resolve any symlinks in i.e. on macOS /tmp -> /private/tmp
        // because this is not allowed for a nix store.
        auto tmpl = nix::absPath(nix::defaultTempDir() / "tests_nix-store.XXXXXX", nullptr, true);
        nixDir = mkdtemp((char *) tmpl.c_str());
#endif

        nixStoreDir = nixDir / "my_nix_store";
        nixStateDir = nixDir / "my_state";
        nixLogDir = nixDir / "my_log";

        // Options documented in `nix help-stores`
        auto nixStoreDirStr = nixStoreDir.string();
        auto nixStateDirStr = nixStateDir.string();
        auto nixLogDirStr = nixLogDir.string();
        const char * p1[] = {"store", nixStoreDirStr.c_str()};
        const char * p2[] = {"state", nixStateDirStr.c_str()};
        const char * p3[] = {"log", nixLogDirStr.c_str()};

        const char ** params[] = {p1, p2, p3, nullptr};

        auto * store = nix_store_open(ctx, "local", params);
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
        if (store)
            nix_store_free(store);
    }

    void SetUp() override
    {
    }

    Store * store = nullptr;

protected:
    void init_local_store()
    {
        store = open_local_store();
    }
};

} // namespace nixC
