#include "registry.hh"
#include "fetchers.hh"
#include "util.hh"
#include "globals.hh"
#include "store-api.hh"
#include "local-fs-store.hh"

#include "fetch-settings.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

std::shared_ptr<Registry> Registry::read(
    const Path & path, RegistryType type)
{
    auto registry = std::make_shared<Registry>(type);

    if (!pathExists(path))
        return std::make_shared<Registry>(type);

    try {

        auto json = nlohmann::json::parse(readFile(path));

        auto version = json.value("version", 0);

        if (version == 2) {
            for (auto & i : json["flakes"]) {
                auto toAttrs = jsonToAttrs(i["to"]);
                Attrs extraAttrs;
                auto j = toAttrs.find("dir");
                if (j != toAttrs.end()) {
                    extraAttrs.insert(*j);
                    toAttrs.erase(j);
                }
                auto exact = i.find("exact");
                registry->entries.push_back(
                    Entry {
                        .from = Input::fromAttrs(jsonToAttrs(i["from"])),
                        .to = Input::fromAttrs(std::move(toAttrs)),
                        .extraAttrs = extraAttrs,
                        .exact = exact != i.end() && exact.value()
                    });
            }
        }

        else
            throw Error("flake registry '%s' has unsupported version %d", path, version);

    } catch (nlohmann::json::exception & e) {
        warn("cannot parse flake registry '%s': %s", path, e.what());
    } catch (Error & e) {
        warn("cannot read flake registry '%s': %s", path, e.what());
    }

    return registry;
}

void Registry::write(const Path & path)
{
    nlohmann::json arr;
    for (auto & entry : entries) {
        nlohmann::json obj;
        obj["from"] = attrsToJSON(entry.from.toAttrs());
        obj["to"] = attrsToJSON(entry.to.toAttrs());
        if (!entry.extraAttrs.empty())
            obj["to"].update(attrsToJSON(entry.extraAttrs));
        if (entry.exact)
            obj["exact"] = true;
        arr.emplace_back(std::move(obj));
    }

    nlohmann::json json;
    json["version"] = 2;
    json["flakes"] = std::move(arr);

    createDirs(dirOf(path));
    writeFile(path, json.dump(2));
}

void Registry::add(
    const Input & from,
    const Input & to,
    const Attrs & extraAttrs)
{
    entries.emplace_back(
        Entry {
            .from = from,
            .to = to,
            .extraAttrs = extraAttrs
        });
}

void Registry::remove(const Input & input)
{
    // FIXME: use C++20 std::erase.
    for (auto i = entries.begin(); i != entries.end(); )
        if (i->from == input)
            i = entries.erase(i);
        else
            ++i;
}

static Path getSystemRegistryPath()
{
    return settings.nixConfDir + "/registry.json";
}

static std::shared_ptr<Registry> getSystemRegistry()
{
    static auto systemRegistry =
        Registry::read(getSystemRegistryPath(), Registry::System);
    return systemRegistry;
}

Path getUserRegistryPath()
{
    return getConfigDir() + "/nix/registry.json";
}

std::shared_ptr<Registry> getUserRegistry()
{
    static auto userRegistry =
        Registry::read(getUserRegistryPath(), Registry::User);
    return userRegistry;
}

std::shared_ptr<Registry> getCustomRegistry(const Path & p)
{
    static auto customRegistry =
        Registry::read(p, Registry::Custom);
    return customRegistry;
}

static std::shared_ptr<Registry> flagRegistry =
    std::make_shared<Registry>(Registry::Flag);

std::shared_ptr<Registry> getFlagRegistry()
{
    return flagRegistry;
}

void overrideRegistry(
    const Input & from,
    const Input & to,
    const Attrs & extraAttrs)
{
    flagRegistry->add(from, to, extraAttrs);
}

static std::shared_ptr<Registry> getGlobalRegistry(ref<Store> store)
{
    static auto reg = [&]() {
        auto path = fetchSettings.flakeRegistry.get();

        if (!hasPrefix(path, "/")) {
            auto storePathDesc = downloadFile(store, path, "flake-registry.json", false).storePath;
            auto storePath = store->makeFixedOutputPathFromCA(storePathDesc);
            if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                store2->addPermRoot(storePath, getCacheDir() + "/nix/flake-registry.json");
            path = store->toRealPath(storePath);
        }

        return Registry::read(path, Registry::Global);
    }();

    return reg;
}

Registries getRegistries(ref<Store> store)
{
    Registries registries;
    registries.push_back(getFlagRegistry());
    registries.push_back(getUserRegistry());
    registries.push_back(getSystemRegistry());
    registries.push_back(getGlobalRegistry(store));
    return registries;
}

std::pair<Input, Attrs> lookupInRegistries(
    ref<Store> store,
    const Input & _input)
{
    Attrs extraAttrs;
    int n = 0;
    Input input(_input);

 restart:

    n++;
    if (n > 100) throw Error("cycle detected in flake registry for '%s'", input.to_string());

    for (auto & registry : getRegistries(store)) {
        // FIXME: O(n)
        for (auto & entry : registry->entries) {
            if (entry.exact) {
                if (entry.from == input) {
                    input = entry.to;
                    extraAttrs = entry.extraAttrs;
                    goto restart;
                }
            } else {
                if (entry.from.contains(input)) {
                    input = entry.to.applyOverrides(
                        !entry.from.getRef() && input.getRef() ? input.getRef() : std::optional<std::string>(),
                        !entry.from.getRev() && input.getRev() ? input.getRev() : std::optional<Hash>());
                    extraAttrs = entry.extraAttrs;
                    goto restart;
                }
            }
        }
    }

    if (!input.isDirect())
        throw Error("cannot find flake '%s' in the flake registries", input.to_string());

    debug("looked up '%s' -> '%s'", _input.to_string(), input.to_string());

    return {input, extraAttrs};
}

}
