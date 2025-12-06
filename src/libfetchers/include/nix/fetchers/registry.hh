#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/source-path.hh"
#include "nix/fetchers/fetchers.hh"

namespace nix {
class Store;
}

namespace nix::fetchers {

struct Registry
{
    enum RegistryType {
        Flag = 0,
        User = 1,
        System = 2,
        Global = 3,
        Custom = 4,
    };

    RegistryType type;

    struct Entry
    {
        Input from, to;
        Attrs extraAttrs;
        bool exact = false;
    };

    std::vector<Entry> entries;

    Registry(RegistryType type)
        : type{type}
    {
    }

    static std::shared_ptr<Registry> read(const Settings & settings, const SourcePath & path, RegistryType type);

    void write(const std::filesystem::path & path);

    void add(const Input & from, const Input & to, const Attrs & extraAttrs, bool exact);

    void remove(const Input & input);
};

typedef std::vector<std::shared_ptr<Registry>> Registries;

std::shared_ptr<Registry> getUserRegistry(const Settings & settings);

std::shared_ptr<Registry> getCustomRegistry(const Settings & settings, const std::filesystem::path & p);

std::filesystem::path getUserRegistryPath();

Registries getRegistries(const Settings & settings, Store & store);

void overrideRegistry(const Input & from, const Input & to, const Attrs & extraAttrs);

enum class UseRegistries : int {
    No,
    All,
    Limited, // global and flag registry only
};

/**
 * Rewrite a flakeref using the registries. If `filter` is set, only
 * use the registries for which the filter function returns true.
 */
std::pair<Input, Attrs>
lookupInRegistries(const Settings & settings, Store & store, const Input & input, UseRegistries useRegistries);

} // namespace nix::fetchers
