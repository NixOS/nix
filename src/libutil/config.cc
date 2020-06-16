#include "config.hh"
#include "args.hh"
#include "json.hh"

namespace nix {

bool Config::set(std::string_view name, std::string_view value)
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

void AbstractConfig::applyConfig(std::string_view contents, std::string_view path) {
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
            auto p = absPath(Path { tokens[1] }, dirOf(path));
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

void AbstractConfig::applyConfigFile(PathView path)
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

void Config::toJSON(JSONObject & out)
{
    for (auto & s : _settings)
        if (!s.second.isAlias) {
            JSONObject out2(out.object(s.first));
            out2.attr("description", std::string_view { s.second.setting->description });
            JSONPlaceholder out3(out2.placeholder("value"));
            s.second.setting->toJSON(out3);
        }
}

void Config::convertToArgs(Args & args, std::string_view category)
{
    for (auto & s : _settings)
        if (!s.second.isAlias)
            s.second.setting->convertToArg(args, category);
}

AbstractSetting::AbstractSetting(
    std::string_view name,
    std::string_view description,
    const std::set<std::string> & aliases)
    : name(name), description(description), aliases(aliases)
{
}

void AbstractSetting::setDefault(std::string_view str)
{
    if (!overriden) set(str);
}

void AbstractSetting::toJSON(JSONPlaceholder & out)
{
    out.write(std::string_view { to_string() });
}

void AbstractSetting::convertToArg(Args & args, std::string_view category)
{
}

template<typename T>
void BaseSetting<T>::toJSON(JSONPlaceholder & out)
{
    out.write(value);
}

void BaseSetting<std::string>::toJSON(JSONPlaceholder & out)
{
    out.write(std::string_view { value });
}

#define MAKE_CONVERT_TO_ARG(args, category) \
    args.addFlag({ \
        .longName = name, \
        .description = description, \
        .category = std::string { category }, \
        .labels = {"value"}, \
        .handler = {[=](std::string s) { overriden = true; set(s); }}, \
    })

template<typename T>
void BaseSetting<T>::convertToArg(Args & args, std::string_view category)
{
    MAKE_CONVERT_TO_ARG(args, category);
}

void BaseSetting<std::string>::convertToArg(Args & args, std::string_view category)
{
    MAKE_CONVERT_TO_ARG(args, category);
}

void BaseSetting<std::string>::set(std::string_view str)
{
    value = str;
}

std::string BaseSetting<std::string>::to_string() const
{
    return value;
}

template<typename T>
void BaseSetting<T>::set(std::string_view str)
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

template<> void BaseSetting<bool>::set(std::string_view str)
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

template<> void BaseSetting<bool>::convertToArg(Args & args, std::string_view category)
{
    args.addFlag({
        .longName = name,
        .description = description,
        .category = std::string { category },
        .handler = {[=]() { override(true); }}
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = description,
        .category = std::string { category },
        .handler = {[=]() { override(false); }}
    });
}

template<> void BaseSetting<Strings>::set(std::string_view str)
{
    value = tokenizeString<Strings>(str);
}

template<> std::string BaseSetting<Strings>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<Strings>::toJSON(JSONPlaceholder & out)
{
    JSONList list(out.list());
    for (auto & s : value)
        list.elem(std::string_view { s });
}

template<> void BaseSetting<StringSet>::set(std::string_view str)
{
    value = tokenizeString<StringSet>(str);
}

template<> std::string BaseSetting<StringSet>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<StringSet>::toJSON(JSONPlaceholder & out)
{
    JSONList list(out.list());
    for (auto & s : value)
        list.elem(std::string_view { s });
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

void PathSetting::set(std::string_view str)
{
    if (str == "") {
        if (allowEmpty)
            value = "";
        else
            throw UsageError("setting '%s' cannot be empty", name);
    } else
        value = canonPath(str);
}

bool GlobalConfig::set(std::string_view name, std::string_view value)
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

void GlobalConfig::toJSON(JSONObject & out)
{
    for (auto & config : *configRegistrations)
        config->toJSON(out);
}

void GlobalConfig::convertToArgs(Args & args, std::string_view category)
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
