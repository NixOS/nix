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
    const Settings & settings;

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

    Registry(const Settings & settings, RegistryType type)
        : settings{settings}
        , type{type}
    {
    }

    static std::shared_ptr<Registry> read(const Settings & settings, const SourcePath & path, RegistryType type);

    void write(const Path & path);

    void add(const Input & from, const Input & to, const Attrs & extraAttrs);

    void remove(const Input & input);
};

typedef std::vector<std::shared_ptr<Registry>> Registries;

std::shared_ptr<Registry> getUserRegistry(const Settings & settings);

std::shared_ptr<Registry> getCustomRegistry(const Settings & settings, const Path & p);

Path getUserRegistryPath();

Registries getRegistries(const Settings & settings, ref<Store> store);

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
std::pair<Input, Attrs> lookupInRegistries(ref<Store> store, const Input & input, UseRegistries useRegistries);

} // namespace nix::fetchers
