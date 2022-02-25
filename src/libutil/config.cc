#include "config.hh"
#include "args.hh"
#include "abstract-setting-to-json.hh"
#include "experimental-features.hh"

#include <nlohmann/json.hpp>

namespace nix {

bool Config::set(const std::string & name, const std::string & value)
{
    bool append = false;
    auto i = _settings.find(name);
    if (i == _settings.end()) {
        if (hasPrefix(name, "extra-")) {
            i = _settings.find(std::string(name, 6));
            if (i == _settings.end() || !i->second.setting->isAppendable())
                return false;
            append = true;
        } else
            return false;
    }
    i->second.setting->set(value, append);
    i->second.setting->overridden = true;
    return true;
}

void Config::addSetting(AbstractSetting * setting)
{
    _settings.emplace(setting->name, Config::SettingData(false, setting));
    for (auto & alias : setting->aliases)
        _settings.emplace(alias, Config::SettingData(true, setting));

    bool set = false;

    auto i = unknownSettings.find(setting->name);
    if (i != unknownSettings.end()) {
        setting->set(i->second);
        setting->overridden = true;
        unknownSettings.erase(i);
        set = true;
    }

    for (auto & alias : setting->aliases) {
        auto i = unknownSettings.find(alias);
        if (i != unknownSettings.end()) {
            if (set)
                warn("setting '%s' is set, but it's an alias of '%s' which is also set",
                    alias, setting->name);
            else {
                setting->set(i->second);
                setting->overridden = true;
                unknownSettings.erase(i);
                set = true;
            }
        }
    }
}

void AbstractConfig::warnUnknownSettings()
{
    for (auto & s : unknownSettings)
        warn("unknown setting '%s'", s.first);
}

void AbstractConfig::reapplyUnknownSettings()
{
    auto unknownSettings2 = std::move(unknownSettings);
    for (auto & s : unknownSettings2)
        set(s.first, s.second);
}

void Config::getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly)
{
    for (auto & opt : _settings)
        if (!opt.second.isAlias && (!overriddenOnly || opt.second.setting->overridden))
            res.emplace(opt.first, SettingInfo{opt.second.setting->to_string(), opt.second.setting->description});
}

void AbstractConfig::applyConfig(const std::string & contents, const std::string & path) {
    unsigned int pos = 0;

    while (pos < contents.size()) {
        std::string line;
        while (pos < contents.size() && contents[pos] != '\n')
            line += contents[pos++];
        pos++;

        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = std::string(line, 0, hash);

        auto tokens = tokenizeString<std::vector<std::string>>(line);
        if (tokens.empty()) continue;

        if (tokens.size() < 2)
            throw UsageError("illegal configuration line '%1%' in '%2%'", line, path);

        auto include = false;
        auto ignoreMissing = false;
        if (tokens[0] == "include")
            include = true;
        else if (tokens[0] == "!include") {
            include = true;
            ignoreMissing = true;
        }

        if (include) {
            if (tokens.size() != 2)
                throw UsageError("illegal configuration line '%1%' in '%2%'", line, path);
            auto p = absPath(tokens[1], dirOf(path));
            if (pathExists(p)) {
                applyConfigFile(p);
            } else if (!ignoreMissing) {
                throw Error("file '%1%' included from '%2%' not found", p, path);
            }
            continue;
        }

        if (tokens[1] != "=")
            throw UsageError("illegal configuration line '%1%' in '%2%'", line, path);

        std::string name = tokens[0];

        auto i = tokens.begin();
        advance(i, 2);

        set(name, concatStringsSep(" ", Strings(i, tokens.end()))); // FIXME: slow
    };
}

void AbstractConfig::applyConfigFile(const Path & path)
{
    try {
        std::string contents = readFile(path);
        applyConfig(contents, path);
    } catch (SysError &) { }
}

void Config::resetOverridden()
{
    for (auto & s : _settings)
        s.second.setting->overridden = false;
}

nlohmann::json Config::toJSON()
{
    auto res = nlohmann::json::object();
    for (auto & s : _settings)
        if (!s.second.isAlias) {
            res.emplace(s.first, s.second.setting->toJSON());
        }
    return res;
}

std::string Config::toKeyValue()
{
    auto res = std::string();
    for (auto & s : _settings)
        if (!s.second.isAlias) {
            res += fmt("%s = %s\n", s.first, s.second.setting->to_string());
        }
    return res;
}

void Config::convertToArgs(Args & args, const std::string & category)
{
    for (auto & s : _settings)
        if (!s.second.isAlias)
            s.second.setting->convertToArg(args, category);
}

AbstractSetting::AbstractSetting(
    const std::string & name,
    const std::string & description,
    const std::set<std::string> & aliases)
    : name(name), description(stripIndentation(description)), aliases(aliases)
{
}

nlohmann::json AbstractSetting::toJSON()
{
    return nlohmann::json(toJSONObject());
}

std::map<std::string, nlohmann::json> AbstractSetting::toJSONObject()
{
    std::map<std::string, nlohmann::json> obj;
    obj.emplace("description", description);
    obj.emplace("aliases", aliases);
    return obj;
}

void AbstractSetting::convertToArg(Args & args, const std::string & category)
{
}

template<typename T>
bool BaseSetting<T>::isAppendable()
{
    return false;
}

template<typename T>
void BaseSetting<T>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = fmt("Set the `%s` setting.", name),
        .category = category,
        .labels = {"value"},
        .handler = {[=](std::string s) { overridden = true; set(s); }},
    });

    if (isAppendable())
        args.addFlag({
            .longName = "extra-" + name,
            .description = fmt("Append to the `%s` setting.", name),
            .category = category,
            .labels = {"value"},
            .handler = {[=](std::string s) { overridden = true; set(s, true); }},
        });
}

template<> void BaseSetting<std::string>::set(const std::string & str, bool append)
{
    value = str;
}

template<> std::string BaseSetting<std::string>::to_string() const
{
    return value;
}

template<typename T>
void BaseSetting<T>::set(const std::string & str, bool append)
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    if (auto n = string2Int<T>(str))
        value = *n;
    else
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
}

template<typename T>
std::string BaseSetting<T>::to_string() const
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    return std::to_string(value);
}

template<> void BaseSetting<bool>::set(const std::string & str, bool append)
{
    if (str == "true" || str == "yes" || str == "1")
        value = true;
    else if (str == "false" || str == "no" || str == "0")
        value = false;
    else
        throw UsageError("Boolean setting '%s' has invalid value '%s'", name, str);
}

template<> std::string BaseSetting<bool>::to_string() const
{
    return value ? "true" : "false";
}

template<> void BaseSetting<bool>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = fmt("Enable the `%s` setting.", name),
        .category = category,
        .handler = {[=]() { override(true); }}
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = fmt("Disable the `%s` setting.", name),
        .category = category,
        .handler = {[=]() { override(false); }}
    });
}

template<> void BaseSetting<Strings>::set(const std::string & str, bool append)
{
    auto ss = tokenizeString<Strings>(str);
    if (!append) value.clear();
    for (auto & s : ss) value.push_back(std::move(s));
}

template<> bool BaseSetting<Strings>::isAppendable()
{
    return true;
}

template<> std::string BaseSetting<Strings>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<StringSet>::set(const std::string & str, bool append)
{
    if (!append) value.clear();
    for (auto & s : tokenizeString<StringSet>(str))
        value.insert(s);
}

template<> bool BaseSetting<StringSet>::isAppendable()
{
    return true;
}

template<> std::string BaseSetting<StringSet>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<std::set<ExperimentalFeature>>::set(const std::string & str, bool append)
{
    if (!append) value.clear();
    for (auto & s : tokenizeString<StringSet>(str)) {
        auto thisXpFeature = parseExperimentalFeature(s);
        if (thisXpFeature)
            value.insert(thisXpFeature.value());
        else
            warn("unknown experimental feature '%s'", s);
    }
}

template<> bool BaseSetting<std::set<ExperimentalFeature>>::isAppendable()
{
    return true;
}

template<> std::string BaseSetting<std::set<ExperimentalFeature>>::to_string() const
{
    StringSet stringifiedXpFeatures;
    for (auto & feature : value)
        stringifiedXpFeatures.insert(std::string(showExperimentalFeature(feature)));
    return concatStringsSep(" ", stringifiedXpFeatures);
}

template<> void BaseSetting<StringMap>::set(const std::string & str, bool append)
{
    if (!append) value.clear();
    for (auto & s : tokenizeString<Strings>(str)) {
        auto eq = s.find_first_of('=');
        if (std::string::npos != eq)
            value.emplace(std::string(s, 0, eq), std::string(s, eq + 1));
        // else ignored
    }
}

template<> bool BaseSetting<StringMap>::isAppendable()
{
    return true;
}

template<> std::string BaseSetting<StringMap>::to_string() const
{
    Strings kvstrs;
    std::transform(value.begin(), value.end(), back_inserter(kvstrs),
        [&](auto kvpair){ return kvpair.first + "=" + kvpair.second; });
    return concatStringsSep(" ", kvstrs);
}

template class BaseSetting<int>;
template class BaseSetting<unsigned int>;
template class BaseSetting<long>;
template class BaseSetting<unsigned long>;
template class BaseSetting<long long>;
template class BaseSetting<unsigned long long>;
template class BaseSetting<bool>;
template class BaseSetting<std::string>;
template class BaseSetting<Strings>;
template class BaseSetting<StringSet>;
template class BaseSetting<StringMap>;
template class BaseSetting<std::set<ExperimentalFeature>>;

void PathSetting::set(const std::string & str, bool append)
{
    if (str == "") {
        if (allowEmpty)
            value = "";
        else
            throw UsageError("setting '%s' cannot be empty", name);
    } else
        value = canonPath(str);
}

bool GlobalConfig::set(const std::string & name, const std::string & value)
{
    for (auto & config : *configRegistrations)
        if (config->set(name, value)) return true;

    unknownSettings.emplace(name, value);

    return false;
}

void GlobalConfig::getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly)
{
    for (auto & config : *configRegistrations)
        config->getSettings(res, overriddenOnly);
}

void GlobalConfig::resetOverridden()
{
    for (auto & config : *configRegistrations)
        config->resetOverridden();
}

nlohmann::json GlobalConfig::toJSON()
{
    auto res = nlohmann::json::object();
    for (auto & config : *configRegistrations)
        res.update(config->toJSON());
    return res;
}

std::string GlobalConfig::toKeyValue()
{
    std::string res;
    std::map<std::string, Config::SettingInfo> settings;
    globalConfig.getSettings(settings);
    for (auto & s : settings)
        res += fmt("%s = %s\n", s.first, s.second.value);
    return res;
}

void GlobalConfig::convertToArgs(Args & args, const std::string & category)
{
    for (auto & config : *configRegistrations)
        config->convertToArgs(args, category);
}

GlobalConfig globalConfig;

GlobalConfig::ConfigRegistrations * GlobalConfig::configRegistrations;

GlobalConfig::Register::Register(Config * config)
{
    if (!configRegistrations)
        configRegistrations = new ConfigRegistrations;
    configRegistrations->emplace_back(config);
}

}
