#include "config.hh"
#include "args.hh"
#include "abstractsettingtojson.hh"

#include <nlohmann/json.hpp>

namespace nix {

bool Config::set(const std::string & name, const std::string & value)
{
    auto i = _settings.find(name);
    if (i == _settings.end()) return false;
    i->second.setting->set(value);
    i->second.setting->overriden = true;
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
        setting->overriden = true;
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
                setting->overriden = true;
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

void Config::getSettings(std::map<std::string, SettingInfo> & res, bool overridenOnly)
{
    for (auto & opt : _settings)
        if (!opt.second.isAlias && (!overridenOnly || opt.second.setting->overriden))
            res.emplace(opt.first, SettingInfo{opt.second.setting->to_string(), opt.second.setting->description});
}

void AbstractConfig::applyConfig(const std::string & contents, const std::string & path) {
    unsigned int pos = 0;

    while (pos < contents.size()) {
        string line;
        while (pos < contents.size() && contents[pos] != '\n')
            line += contents[pos++];
        pos++;

        string::size_type hash = line.find('#');
        if (hash != string::npos)
            line = string(line, 0, hash);

        vector<string> tokens = tokenizeString<vector<string> >(line);
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

        string name = tokens[0];

        vector<string>::iterator i = tokens.begin();
        advance(i, 2);

        set(name, concatStringsSep(" ", Strings(i, tokens.end()))); // FIXME: slow
    };
}

void AbstractConfig::applyConfigFile(const Path & path)
{
    try {
        string contents = readFile(path);
        applyConfig(contents, path);
    } catch (SysError &) { }
}

void Config::resetOverriden()
{
    for (auto & s : _settings)
        s.second.setting->overriden = false;
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

void AbstractSetting::setDefault(const std::string & str)
{
    if (!overriden) set(str);
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
void BaseSetting<T>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = description,
        .category = category,
        .labels = {"value"},
        .handler = {[=](std::string s) { overriden = true; set(s); }},
    });
}

template<> void BaseSetting<std::string>::set(const std::string & str)
{
    value = str;
}

template<> std::string BaseSetting<std::string>::to_string() const
{
    return value;
}

template<typename T>
void BaseSetting<T>::set(const std::string & str)
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    if (!string2Int(str, value))
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
}

template<typename T>
std::string BaseSetting<T>::to_string() const
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    return std::to_string(value);
}

template<> void BaseSetting<bool>::set(const std::string & str)
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
        .description = description,
        .category = category,
        .handler = {[=]() { override(true); }}
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = description,
        .category = category,
        .handler = {[=]() { override(false); }}
    });
}

template<> void BaseSetting<Strings>::set(const std::string & str)
{
    value = tokenizeString<Strings>(str);
}

template<> std::string BaseSetting<Strings>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<StringSet>::set(const std::string & str)
{
    value = tokenizeString<StringSet>(str);
}

template<> std::string BaseSetting<StringSet>::to_string() const
{
    return concatStringsSep(" ", value);
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

void PathSetting::set(const std::string & str)
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

void GlobalConfig::getSettings(std::map<std::string, SettingInfo> & res, bool overridenOnly)
{
    for (auto & config : *configRegistrations)
        config->getSettings(res, overridenOnly);
}

void GlobalConfig::resetOverriden()
{
    for (auto & config : *configRegistrations)
        config->resetOverriden();
}

nlohmann::json GlobalConfig::toJSON()
{
    auto res = nlohmann::json::object();
    for (auto & config : *configRegistrations)
        res.update(config->toJSON());
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
