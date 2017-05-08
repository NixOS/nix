#include "config.hh"
#include "args.hh"
#include "json.hh"

namespace nix {

void Config::set(const std::string & name, const std::string & value)
{
    auto i = _settings.find(name);
    if (i == _settings.end())
        throw UsageError("unknown setting '%s'", name);
    i->second.setting->set(value);
    i->second.setting->overriden = true;
}

void Config::addSetting(AbstractSetting * setting)
{
    _settings.emplace(setting->name, Config::SettingData(false, setting));
    for (auto & alias : setting->aliases)
        _settings.emplace(alias, Config::SettingData(true, setting));

    bool set = false;

    auto i = initials.find(setting->name);
    if (i != initials.end()) {
        setting->set(i->second);
        setting->overriden = true;
        initials.erase(i);
        set = true;
    }

    for (auto & alias : setting->aliases) {
        auto i = initials.find(alias);
        if (i != initials.end()) {
            if (set)
                warn("setting '%s' is set, but it's an alias of '%s' which is also set",
                    alias, setting->name);
            else {
                setting->set(i->second);
                setting->overriden = true;
                initials.erase(i);
                set = true;
            }
        }
    }
}

void Config::warnUnknownSettings()
{
    for (auto & i : initials)
        warn("unknown setting '%s'", i.first);
}

StringMap Config::getSettings(bool overridenOnly)
{
    StringMap res;
    for (auto & opt : _settings)
        if (!opt.second.isAlias && (!overridenOnly || opt.second.setting->overriden))
            res.emplace(opt.first, opt.second.setting->to_string());
    return res;
}

void Config::applyConfigFile(const Path & path, bool fatal)
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

            if (tokens.size() < 2 || tokens[1] != "=")
                throw UsageError("illegal configuration line '%1%' in '%2%'", line, path);

            string name = tokens[0];

            vector<string>::iterator i = tokens.begin();
            advance(i, 2);

            try {
                set(name, concatStringsSep(" ", Strings(i, tokens.end()))); // FIXME: slow
            } catch (UsageError & e) {
                if (fatal) throw;
                warn("in configuration file '%s': %s", path, e.what());
            }
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

AbstractSetting::AbstractSetting(
    const std::string & name,
    const std::string & description,
    const std::set<std::string> & aliases)
    : name(name), description(description), aliases(aliases)
{
}

void AbstractSetting::toJSON(JSONPlaceholder & out)
{
    out.write(to_string());
}

template<typename T>
void BaseSetting<T>::toJSON(JSONPlaceholder & out)
{
    out.write(value);
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
