#include "parsed-derivations.hh"

#include <nlohmann/json.hpp>
#include <regex>
#include "json.hh"

namespace nix {

ParsedDerivation::ParsedDerivation(const StorePath & drvPath, BasicDerivation & drv)
    : drvPath(drvPath), drv(drv)
{
    /* Parse the __json attribute, if any. */
    auto jsonAttr = drv.env.find("__json");
    if (jsonAttr != drv.env.end()) {
        try {
            structuredAttrs = std::make_unique<nlohmann::json>(nlohmann::json::parse(jsonAttr->second));
        } catch (std::exception & e) {
            throw Error("cannot process __json attribute of '%s': %s", drvPath.to_string(), e.what());
        }
    }
}

ParsedDerivation::~ParsedDerivation() { }

std::optional<std::string> ParsedDerivation::getStringAttr(const std::string & name) const
{
    if (structuredAttrs) {
        auto i = structuredAttrs->find(name);
        if (i == structuredAttrs->end())
            return {};
        else {
            if (!i->is_string())
                throw Error("attribute '%s' of derivation '%s' must be a string", name, drvPath.to_string());
            return i->get<std::string>();
        }
    } else {
        auto i = drv.env.find(name);
        if (i == drv.env.end())
            return {};
        else
            return i->second;
    }
}

bool ParsedDerivation::getBoolAttr(const std::string & name, bool def) const
{
    if (structuredAttrs) {
        auto i = structuredAttrs->find(name);
        if (i == structuredAttrs->end())
            return def;
        else {
            if (!i->is_boolean())
                throw Error("attribute '%s' of derivation '%s' must be a Boolean", name, drvPath.to_string());
            return i->get<bool>();
        }
    } else {
        auto i = drv.env.find(name);
        if (i == drv.env.end())
            return def;
        else
            return i->second == "1";
    }
}

std::optional<Strings> ParsedDerivation::getStringsAttr(const std::string & name) const
{
    if (structuredAttrs) {
        auto i = structuredAttrs->find(name);
        if (i == structuredAttrs->end())
            return {};
        else {
            if (!i->is_array())
                throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, drvPath.to_string());
            Strings res;
            for (auto j = i->begin(); j != i->end(); ++j) {
                if (!j->is_string())
                    throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, drvPath.to_string());
                res.push_back(j->get<std::string>());
            }
            return res;
        }
    } else {
        auto i = drv.env.find(name);
        if (i == drv.env.end())
            return {};
        else
            return tokenizeString<Strings>(i->second);
    }
}

StringSet ParsedDerivation::getRequiredSystemFeatures() const
{
    StringSet res;
    for (auto & i : getStringsAttr("requiredSystemFeatures").value_or(Strings()))
        res.insert(i);
    if (!drv.type().hasKnownOutputPaths())
        res.insert("ca-derivations");
    return res;
}

bool ParsedDerivation::canBuildLocally(Store & localStore) const
{
    if (drv.platform != settings.thisSystem.get()
        && !settings.extraPlatforms.get().count(drv.platform)
        && !drv.isBuiltin())
        return false;

    if (settings.maxBuildJobs.get() == 0
        && !drv.isBuiltin())
        return false;

    for (auto & feature : getRequiredSystemFeatures())
        if (!localStore.systemFeatures.get().count(feature)) return false;

    return true;
}

bool ParsedDerivation::willBuildLocally(Store & localStore) const
{
    return getBoolAttr("preferLocalBuild") && canBuildLocally(localStore);
}

bool ParsedDerivation::substitutesAllowed() const
{
    return getBoolAttr("allowSubstitutes", true);
}

static std::regex shVarName("[A-Za-z_][A-Za-z0-9_]*");

std::optional<nlohmann::json> ParsedDerivation::prepareStructuredAttrs(Store & store, const StorePathSet & inputPaths)
{
    auto structuredAttrs = getStructuredAttrs();
    if (!structuredAttrs) return std::nullopt;

    auto json = *structuredAttrs;

    /* Add an "outputs" object containing the output paths. */
    nlohmann::json outputs;
    for (auto & i : drv.outputs)
        outputs[i.first] = hashPlaceholder(i.first);
    json["outputs"] = outputs;

    /* Handle exportReferencesGraph. */
    auto e = json.find("exportReferencesGraph");
    if (e != json.end() && e->is_object()) {
        for (auto i = e->begin(); i != e->end(); ++i) {
            std::ostringstream str;
            {
                JSONPlaceholder jsonRoot(str, true);
                StorePathSet storePaths;
                for (auto & p : *i)
                    storePaths.insert(store.parseStorePath(p.get<std::string>()));
                store.pathInfoToJSON(jsonRoot,
                    store.exportReferences(storePaths, inputPaths), false, true);
            }
            json[i.key()] = nlohmann::json::parse(str.str()); // urgh
        }
    }

    return json;
}

/* As a convenience to bash scripts, write a shell file that
   maps all attributes that are representable in bash -
   namely, strings, integers, nulls, Booleans, and arrays and
   objects consisting entirely of those values. (So nested
   arrays or objects are not supported.) */
std::string writeStructuredAttrsShell(const nlohmann::json & json)
{

    auto handleSimpleType = [](const nlohmann::json & value) -> std::optional<std::string> {
        if (value.is_string())
            return shellEscape(value.get<std::string_view>());

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

    for (auto & [key, value] : json.items()) {

        if (!std::regex_match(key, shVarName)) continue;

        auto s = handleSimpleType(value);
        if (s)
            jsonSh += fmt("declare %s=%s\n", key, *s);

        else if (value.is_array()) {
            std::string s2;
            bool good = true;

            for (auto & value2 : value) {
                auto s3 = handleSimpleType(value2);
                if (!s3) { good = false; break; }
                s2 += *s3; s2 += ' ';
            }

            if (good)
                jsonSh += fmt("declare -a %s=(%s)\n", key, s2);
        }

        else if (value.is_object()) {
            std::string s2;
            bool good = true;

            for (auto & [key2, value2] : value.items()) {
                auto s3 = handleSimpleType(value2);
                if (!s3) { good = false; break; }
                s2 += fmt("[%s]=%s ", shellEscape(key2), *s3);
            }

            if (good)
                jsonSh += fmt("declare -A %s=(%s)\n", key, s2);
        }
    }

    return jsonSh;
}
}
