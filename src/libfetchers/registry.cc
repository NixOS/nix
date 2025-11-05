#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/registry.hh"
#include "nix/fetchers/tarball.hh"
#include "nix/util/users.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-api.hh"
#include "nix/store/local-fs-store.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

std::shared_ptr<Registry> Registry::read(const Settings & settings, const SourcePath & path, RegistryType type)
{
    debug("reading registry '%s'", path);

    auto registry = std::make_shared<Registry>(settings, type);

    if (!path.pathExists())
        return std::make_shared<Registry>(settings, type);

    try {

        auto json = nlohmann::json::parse(path.readFile());

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
                    Entry{
                        .from = Input::fromAttrs(settings, jsonToAttrs(i["from"])),
                        .to = Input::fromAttrs(settings, std::move(toAttrs)),
                        .extraAttrs = extraAttrs,
                        .exact = exact != i.end() && exact.value()});
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

void Registry::add(const Input & from, const Input & to, const Attrs & extraAttrs)
{
    entries.emplace_back(Entry{.from = from, .to = to, .extraAttrs = extraAttrs});
}

void Registry::remove(const Input & input)
{
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const Entry & entry) { return entry.from == input; }),
        entries.end());
}

static Path getSystemRegistryPath()
{
    return settings.nixConfDir + "/registry.json";
}

static std::shared_ptr<Registry> getSystemRegistry(const Settings & settings)
{
    static auto systemRegistry = Registry::read(
        settings,
        SourcePath{getFSSourceAccessor(), CanonPath{getSystemRegistryPath()}}.resolveSymlinks(),
        Registry::System);
    return systemRegistry;
}

Path getUserRegistryPath()
{
    return getConfigDir() + "/registry.json";
}

std::shared_ptr<Registry> getUserRegistry(const Settings & settings)
{
    static auto userRegistry = Registry::read(
        settings,
        SourcePath{getFSSourceAccessor(), CanonPath{getUserRegistryPath()}}.resolveSymlinks(),
        Registry::User);
    return userRegistry;
}

std::shared_ptr<Registry> getCustomRegistry(const Settings & settings, const Path & p)
{
    static auto customRegistry =
        Registry::read(settings, SourcePath{getFSSourceAccessor(), CanonPath{p}}.resolveSymlinks(), Registry::Custom);
    return customRegistry;
}

std::shared_ptr<Registry> getFlagRegistry(const Settings & settings)
{
    static auto flagRegistry = std::make_shared<Registry>(settings, Registry::Flag);
    return flagRegistry;
}

void overrideRegistry(const Input & from, const Input & to, const Attrs & extraAttrs)
{
    getFlagRegistry(*from.settings)->add(from, to, extraAttrs);
}

static std::shared_ptr<Registry> getGlobalRegistry(const Settings & settings, ref<Store> store)
{
    static auto reg = [&]() {
        auto path = settings.flakeRegistry.get();
        if (path == "") {
            return std::make_shared<Registry>(settings, Registry::Global); // empty registry
        }

        return Registry::read(
            settings,
            [&] -> SourcePath {
                if (!isAbsolute(path)) {
                    auto storePath = downloadFile(store, settings, path, "flake-registry.json").storePath;
                    if (auto store2 = store.dynamic_pointer_cast<LocalFSStore>())
                        store2->addPermRoot(storePath, getCacheDir() + "/flake-registry.json");
                    return {store->requireStoreObjectAccessor(storePath)};
                } else {
                    return SourcePath{getFSSourceAccessor(), CanonPath{path}}.resolveSymlinks();
                }
            }(),
            Registry::Global);
    }();

    return reg;
}

Registries getRegistries(const Settings & settings, ref<Store> store)
{
    Registries registries;
    registries.push_back(getFlagRegistry(settings));
    registries.push_back(getUserRegistry(settings));
    registries.push_back(getSystemRegistry(settings));
    registries.push_back(getGlobalRegistry(settings, store));
    return registries;
}

std::pair<Input, Attrs> lookupInRegistries(ref<Store> store, const Input & _input, UseRegistries useRegistries)
{
    Attrs extraAttrs;
    int n = 0;
    Input input(_input);

    if (useRegistries == UseRegistries::No)
        return {input, extraAttrs};

restart:

    n++;
    if (n > 100)
        throw Error("cycle detected in flake registry for '%s'", input.to_string());

    for (auto & registry : getRegistries(*input.settings, store)) {
        if (useRegistries == UseRegistries::Limited
            && !(registry->type == fetchers::Registry::Flag || registry->type == fetchers::Registry::Global))
            continue;
        // FIXME: O(n)
        for (auto & entry : registry->entries) {
            if (entry.exact) {
                if (entry.from == input) {
                    debug("resolved flakeref '%s' against registry %d exactly", input.to_string(), registry->type);
                    input = entry.to;
                    extraAttrs = entry.extraAttrs;
                    goto restart;
                }
            } else {
                if (entry.from.contains(input)) {
                    debug("resolved flakeref '%s' against registry %d", input.to_string(), registry->type);
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

} // namespace nix::fetchers
