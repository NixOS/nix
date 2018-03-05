#include "config.hh"
#include "args.hh"
#include "json.hh"

namespace nix {

void Config::set(const std::string & name, const std::string & value)
{
    auto i = _settings.find(name);
    if (i == _settings.end()) {
        extras.emplace(name, value);
    } else {
        i->second.setting->set(value);
        i->second.setting->overriden = true;
    }
}

void Config::addSetting(AbstractSetting * setting)
{
    _settings.emplace(setting->name, Config::SettingData(false, setting));
    for (auto & alias : setting->aliases)
        _settings.emplace(alias, Config::SettingData(true, setting));

    bool set = false;

    auto i = extras.find(setting->name);
    if (i != extras.end()) {
        setting->set(i->second);
        setting->overriden = true;
        extras.erase(i);
        set = true;
    }

    for (auto & alias : setting->aliases) {
        auto i = extras.find(alias);
        if (i != extras.end()) {
            if (set)
                warn("setting '%s' is set, but it's an alias of '%s' which is also set",
                    alias, setting->name);
            else {
                setting->set(i->second);
                setting->overriden = true;
                extras.erase(i);
                set = true;
            }
        }
    }
}

void Config::handleUnknownSettings()
{
    for (auto & s : extras)
        warn("unknown setting '%s'", s.first);
}

StringMap Config::getSettings(SettingsSubset subset)
{
    StringMap res;
    auto predicate = subset == all ?
        [](const Settings::value_type & it){ return true; } :
        subset == overriddenOnly ?
            [](const Settings::value_type & it){ return it.second.setting->overriden; } :
            [](const Settings::value_type & it){ return it.second.setting->forwardable; };
    Settings opts;
    for (auto & opt : _settings)
        if (predicate(opt))
            res.emplace(opt.first, opt.second.setting->to_string());
    return res;
}

void Config::applyConfigFile(const Path & path)
{
    try {
        string contents = readFile(path);

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
            out2.attr("description", s.second.setting->description);
            JSONPlaceholder out3(out2.placeholder("value"));
            s.second.setting->toJSON(out3);
        }
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
    const std::set<std::string> & aliases,
    bool forwardable)
    : name(name), description(description), aliases(aliases), forwardable(forwardable)
{
}

void AbstractSetting::toJSON(JSONPlaceholder & out)
{
    out.write(to_string());
}

void AbstractSetting::convertToArg(Args & args, const std::string & category)
{
}

template<typename T>
void BaseSetting<T>::toJSON(JSONPlaceholder & out)
{
    out.write(value);
}

template<typename T>
void BaseSetting<T>::convertToArg(Args & args, const std::string & category)
{
    args.mkFlag()
        .longName(name)
        .description(description)
        .arity(1)
        .handler([=](std::vector<std::string> ss) { overriden = true; set(ss[0]); })
        .category(category);
}

template<> void BaseSetting<std::string>::set(const std::string & str)
{
    value = str;
}

template<> std::string BaseSetting<std::string>::to_string()
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
std::string BaseSetting<T>::to_string()
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

template<> std::string BaseSetting<bool>::to_string()
{
    return value ? "true" : "false";
}

template<> void BaseSetting<bool>::convertToArg(Args & args, const std::string & category)
{
    args.mkFlag()
        .longName(name)
        .description(description)
        .handler([=](std::vector<std::string> ss) { override(true); })
        .category(category);
    args.mkFlag()
        .longName("no-" + name)
        .description(description)
        .handler([=](std::vector<std::string> ss) { override(false); })
        .category(category);
}

template<> void BaseSetting<Strings>::set(const std::string & str)
{
    value = tokenizeString<Strings>(str);
}

template<> std::string BaseSetting<Strings>::to_string()
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<Strings>::toJSON(JSONPlaceholder & out)
{
    JSONList list(out.list());
    for (auto & s : value)
        list.elem(s);
}

template<> void BaseSetting<StringSet>::set(const std::string & str)
{
    value = tokenizeString<StringSet>(str);
}

template<> std::string BaseSetting<StringSet>::to_string()
{
    return concatStringsSep(" ", value);
}

template<> void BaseSetting<StringSet>::toJSON(JSONPlaceholder & out)
{
    JSONList list(out.list());
    for (auto & s : value)
        list.elem(s);
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

}
