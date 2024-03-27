#include <fstream>
#include <regex>
#include <nlohmann/json.hpp>

#include "archive.hh"
#include "json-meta-store.hh"
#include "callback.hh"
#include "url.hh"
#include "path-info.hh"
#include "realisation.hh"

namespace nix {

std::string JsonMetaStoreConfig::doc()
{
    return
        "#include json-meta-store.md"
        ;
}


JsonMetaStore::JsonMetaStore(const Params & params)
    : StoreConfig(params)
    , LocalFSStoreConfig(params)
    , JsonMetaStoreConfig(params)
    , Store(params)
    , LocalFSStore(params)
{
}


JsonMetaStore::JsonMetaStore(
    const std::string scheme,
    std::string path,
    const Params & params)
    : JsonMetaStore(params)
{
    if (!path.empty())
        throw UsageError("json-meta:// store url doesn't support path part, only scheme and query params");
}


std::string JsonMetaStore::getUri()
{
    return *uriSchemes().begin() + "://";
}


void JsonMetaStore::queryPathInfoUncached(
    const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    using nlohmann::json;

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));
    try
    {
        auto info_path = metaDir.get() + "/object/" + path.hashPart() + ".json";
        try {
            std::ifstream f { info_path };
            auto json = json::parse(f);
            auto info = std::make_shared<ValidPathInfo>(
                path,
                UnkeyedValidPathInfo::fromJSON(*this, json));
            return (*callbackPtr)(std::move(info));
        } catch (SysError &) {
            return (*callbackPtr)({});
        }
    } catch (...) {
        return callbackPtr->rethrow();
    }
}


void JsonMetaStore::queryRealisationUncached(
    const DrvOutput & drvOutput,
    Callback<std::shared_ptr<const Realisation>> callback) noexcept
{
    using nlohmann::json;

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));
    try
    {
        auto realisation_path = metaDir.get() + "/realisation/" + drvOutput.to_string() + ".json";
        try {
            std::ifstream f { realisation_path };
            auto json = json::parse(f);
            auto realisation = std::make_shared<Realisation>(
                Realisation::fromJSON(json, realisation_path));
            return (*callbackPtr)(std::move(realisation));
        } catch (SysError &) {
            return (*callbackPtr)({});
        }
    } catch (...) {
        return callbackPtr->rethrow();
    }
}


// Unimplemented methods


std::optional<StorePath> JsonMetaStore::queryPathFromHashPart(
    const std::string & hashPart)
{ unsupported("queryPathFromHashPart"); }


void JsonMetaStore::addToStore(
    const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs)
{ unsupported("addToStore"); }


StorePath JsonMetaStore::addTextToStore(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references,
    RepairFlag repair)
{ unsupported("addTextToStore"); }


Roots JsonMetaStore::findRoots(bool censor)
{ unsupported("findRoots"); }


void JsonMetaStore::collectGarbage(const GCOptions & options, GCResults & results)
{ unsupported("collectGarbage"); }


static RegisterStoreImplementation<JsonMetaStore, JsonMetaStoreConfig> regJsonMetaStore;

}
