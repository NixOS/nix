#include "nix/store/parsed-derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"

#include <nlohmann/json.hpp>
#include <regex>

namespace nix {

StructuredAttrs StructuredAttrs::parse(std::string_view encoded)
{
    try {
        return StructuredAttrs{
            .structuredAttrs = nlohmann::json::parse(encoded),
        };
    } catch (std::exception & e) {
        throw Error("cannot process %s attribute: %s", envVarName, e.what());
    }
}

std::optional<StructuredAttrs> StructuredAttrs::tryExtract(StringPairs & env)
{
    /* Parse the __json attribute, if any. */
    auto jsonAttr = env.find(envVarName);
    if (jsonAttr != env.end()) {
        auto encoded = std::move(jsonAttr->second);
        env.erase(jsonAttr);
        return parse(encoded);
    } else
        return {};
}

std::pair<std::string_view, std::string> StructuredAttrs::unparse() const
{
    // TODO don't copy the JSON object just to dump it.
    return {envVarName, static_cast<nlohmann::json>(structuredAttrs).dump()};
}

void StructuredAttrs::checkKeyNotInUse(const StringPairs & env)
{
    if (env.count(envVarName))
        throw Error(
            "Cannot have an environment variable named '__json'. This key is reserved for encoding structured attrs");
}

static std::regex shVarName("[A-Za-z_][A-Za-z0-9_]*");

/**
 * Write a JSON representation of store object metadata, such as the
 * hash and the references.
 *
 * @note Do *not* use `ValidPathInfo::toJSON` because this function is
 * subject to stronger stability requirements since it is used to
 * prepare build environments. Perhaps someday we'll have a versionining
 * mechanism to allow this to evolve again and get back in sync, but for
 * now we must not change - not even extend - the behavior.
 */
static nlohmann::json pathInfoToJSON(Store & store, const StorePathSet & storePaths)
{
    using nlohmann::json;

    nlohmann::json::array_t jsonList = json::array();

    for (auto & storePath : storePaths) {
        auto info = store.queryPathInfo(storePath);

        auto & jsonPath = jsonList.emplace_back(json::object());

        jsonPath["narHash"] = info->narHash.to_string(HashFormat::Nix32, true);
        jsonPath["narSize"] = info->narSize;

        {
            auto & jsonRefs = jsonPath["references"] = json::array();
            for (auto & ref : info->references)
                jsonRefs.emplace_back(store.printStorePath(ref));
        }

        if (info->ca)
            jsonPath["ca"] = renderContentAddress(info->ca);

        // Add the path to the object whose metadata we are including.
        jsonPath["path"] = store.printStorePath(storePath);

        jsonPath["valid"] = true;

        jsonPath["closureSize"] = ({
            uint64_t totalNarSize = 0;
            StorePathSet closure;
            store.computeFSClosure(info->path, closure, false, false);
            for (auto & p : closure) {
                auto info = store.queryPathInfo(p);
                totalNarSize += info->narSize;
            }
            totalNarSize;
        });
    }
    return jsonList;
}

nlohmann::json::object_t StructuredAttrs::prepareStructuredAttrs(
    Store & store,
    const DerivationOptions & drvOptions,
    const StorePathSet & inputPaths,
    const DerivationOutputs & outputs) const
{
    /* Copy to then modify */
    auto json = structuredAttrs;

    /* Add an "outputs" object containing the output paths. */
    nlohmann::json outputsJson;
    for (auto & i : outputs)
        outputsJson[i.first] = hashPlaceholder(i.first);
    json["outputs"] = std::move(outputsJson);

    /* Handle exportReferencesGraph. */
    for (auto & [key, storePaths] : drvOptions.getParsedExportReferencesGraph(store)) {
        json[key] = pathInfoToJSON(store, store.exportReferences(storePaths, storePaths));
    }

    return json;
}

std::string StructuredAttrs::writeShell(const nlohmann::json::object_t & json)
{

    auto handleSimpleType = [](const nlohmann::json & value) -> std::optional<std::string> {
        if (value.is_string())
            return escapeShellArgAlways(value.get<std::string_view>());

        if (value.is_number()) {
            auto f = value.get<float>();
            if (std::ceil(f) == f)
                return std::to_string(value.get<int>());
        }

        if (value.is_null())
            return std::string("''");

        if (value.is_boolean())
            return value.get<bool>() ? std::string("1") : std::string("");

        return {};
    };

    std::string jsonSh;

    for (auto & [key, value] : json) {

        if (!std::regex_match(key, shVarName))
            continue;

        auto s = handleSimpleType(value);
        if (s)
            jsonSh += fmt("declare %s=%s\n", key, *s);

        else if (value.is_array()) {
            std::string s2;
            bool good = true;

            for (auto & value2 : value) {
                auto s3 = handleSimpleType(value2);
                if (!s3) {
                    good = false;
                    break;
                }
                s2 += *s3;
                s2 += ' ';
            }

            if (good)
                jsonSh += fmt("declare -a %s=(%s)\n", key, s2);
        }

        else if (value.is_object()) {
            std::string s2;
            bool good = true;

            for (auto & [key2, value2] : value.items()) {
                auto s3 = handleSimpleType(value2);
                if (!s3) {
                    good = false;
                    break;
                }
                s2 += fmt("[%s]=%s ", escapeShellArgAlways(key2), *s3);
            }

            if (good)
                jsonSh += fmt("declare -A %s=(%s)\n", key, s2);
        }
    }

    return jsonSh;
}

} // namespace nix
