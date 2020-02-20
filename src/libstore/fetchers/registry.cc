#include "registry.hh"
#include "util.hh"
#include "fetchers.hh"
#include "globals.hh"
#include "download.hh"

#include <nlohmann/json.hpp>

namespace nix::fetchers {

std::shared_ptr<Registry> Registry::read(
    const Path & path, RegistryType type)
{
    auto registry = std::make_shared<Registry>(type);

    if (!pathExists(path))
        return std::make_shared<Registry>(type);

    auto json = nlohmann::json::parse(readFile(path));

    auto version = json.value("version", 0);

    // FIXME: remove soon
    if (version == 1) {
        auto flakes = json["flakes"];
        for (auto i = flakes.begin(); i != flakes.end(); ++i) {
            auto url = i->value("url", i->value("uri", ""));
            if (url.empty())
                throw Error("flake registry '%s' lacks a 'url' attribute for entry '%s'",
                    path, i.key());
            registry->entries.push_back(
                {inputFromURL(i.key()), inputFromURL(url), {}});
        }
    }

    else if (version == 2) {
        for (auto & i : json["flakes"]) {
            auto toAttrs = jsonToAttrs(i["to"]);
            Input::Attrs extraAttrs;
            auto j = toAttrs.find("dir");
            if (j != toAttrs.end()) {
                extraAttrs.insert(*j);
                toAttrs.erase(j);
            }
            registry->entries.push_back(
                { inputFromAttrs(jsonToAttrs(i["from"]))
                , inputFromAttrs(toAttrs)
                , extraAttrs
                });
        }
    }

    else
        throw Error("flake registry '%s' has unsupported version %d", path, version);


    return registry;
}

void Registry::write(const Path & path)
{
    nlohmann::json arr;
    for (auto & elem : entries) {
        nlohmann::json obj;
        obj["from"] = attrsToJson(std::get<0>(elem)->toAttrs());
        obj["to"] = attrsToJson(std::get<1>(elem)->toAttrs());
        if (!std::get<2>(elem).empty())
            obj["to"].update(attrsToJson(std::get<2>(elem)));
        arr.emplace_back(std::move(obj));
    }

    nlohmann::json json;
    json["version"] = 2;
    json["flakes"] = std::move(arr);

    createDirs(dirOf(path));
    writeFile(path, json.dump(2));
}

void Registry::add(
    const std::shared_ptr<const Input> & from,
    const std::shared_ptr<const Input> & to,
    const Input::Attrs & extraAttrs)
{
    entries.emplace_back(from, to, extraAttrs);
}

void Registry::remove(const std::shared_ptr<const Input> & input)
{
    // FIXME: use C++20 std::erase.
    for (auto i = entries.begin(); i != entries.end(); )
        if (*std::get<0>(*i) == *input)
            i = entries.erase(i);
        else
            ++i;
}

Path getUserRegistryPath()
{
    return getHome() + "/.config/nix/registry.json";
}

std::shared_ptr<Registry> getUserRegistry()
{
    return Registry::read(getUserRegistryPath(), Registry::User);
}

static std::shared_ptr<Registry> flagRegistry =
    std::make_shared<Registry>(Registry::Flag);

std::shared_ptr<Registry> getFlagRegistry()
{
    return flagRegistry;
}

void overrideRegistry(
    const std::shared_ptr<const Input> & from,
    const std::shared_ptr<const Input> & to,
    const Input::Attrs & extraAttrs)
{
    flagRegistry->add(from, to, extraAttrs);
}

static std::shared_ptr<Registry> getGlobalRegistry(ref<Store> store)
{
    static auto reg = [&]() {
        auto path = settings.flakeRegistry;

        if (!hasPrefix(path, "/")) {
            CachedDownloadRequest request(path);
            request.name = "flake-registry.json";
            request.gcRoot = true;
            path = getDownloader()->downloadCached(store, request).path;
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
    registries.push_back(getGlobalRegistry(store));
    return registries;
}

std::pair<std::shared_ptr<const Input>, Input::Attrs> lookupInRegistries(
    ref<Store> store,
    std::shared_ptr<const Input> input)
{
    Input::Attrs extraAttrs;
    int n = 0;

 restart:

    n++;
    if (n > 100) throw Error("cycle detected in flake registr for '%s'", input);

    for (auto & registry : getRegistries(store)) {
        // FIXME: O(n)
        for (auto & entry : registry->entries) {
            auto from = std::get<0>(entry);
            if (from->contains(*input)) {
                input = std::get<1>(entry)->applyOverrides(
                    !from->getRef() && input->getRef() ? input->getRef() : std::optional<std::string>(),
                    !from->getRev() && input->getRev() ? input->getRev() : std::optional<Hash>());
                extraAttrs = std::get<2>(entry);
                goto restart;
            }
        }
    }

    if (!input->isDirect())
        throw Error("cannot find flake '%s' in the flake registries", input->to_string());

    return {input, extraAttrs};
}

}
