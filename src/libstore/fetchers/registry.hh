#pragma once

#include "types.hh"

namespace nix { class Store; }

namespace nix::fetchers {

struct Input;

struct Registry
{
    enum RegistryType {
        Flag = 0,
        User = 1,
        Global = 2,
    };

    RegistryType type;

    std::vector<std::pair<std::shared_ptr<const Input>, std::shared_ptr<const Input>>> entries;

    static std::shared_ptr<Registry> read(
        const Path & path, RegistryType type);

    void write(const Path & path);

    void add(
        const std::shared_ptr<const Input> & from,
        const std::shared_ptr<const Input> & to);

    void remove(const std::shared_ptr<const Input> & input);
};

typedef std::vector<std::shared_ptr<Registry>> Registries;

std::shared_ptr<Registry> getUserRegistry();

Path getUserRegistryPath();

Registries getRegistries(ref<Store> store);

std::shared_ptr<const Input> lookupInRegistries(
    ref<Store> store,
    std::shared_ptr<const Input> input);

}
