#pragma once

#include "types.hh"
#include "fetchers.hh"

namespace nix { class Store; }

namespace nix::fetchers {

struct Registry
{
    enum RegistryType {
        Flag = 0,
        User = 1,
        Global = 2,
    };

    RegistryType type;

    std::vector<
        std::tuple<
            std::shared_ptr<const Input>, // from
            std::shared_ptr<const Input>, // to
            Input::Attrs // extra attributes
            >
        > entries;

    Registry(RegistryType type)
        : type(type)
    { }

    static std::shared_ptr<Registry> read(
        const Path & path, RegistryType type);

    void write(const Path & path);

    void add(
        const std::shared_ptr<const Input> & from,
        const std::shared_ptr<const Input> & to,
        const Input::Attrs & extraAttrs);

    void remove(const std::shared_ptr<const Input> & input);
};

typedef std::vector<std::shared_ptr<Registry>> Registries;

std::shared_ptr<Registry> getUserRegistry();

Path getUserRegistryPath();

Registries getRegistries(ref<Store> store);

void overrideRegistry(
    const std::shared_ptr<const Input> & from,
    const std::shared_ptr<const Input> & to,
    const Input::Attrs & extraAttrs);

std::pair<std::shared_ptr<const Input>, Input::Attrs> lookupInRegistries(
    ref<Store> store,
    std::shared_ptr<const Input> input);

}
