#include "config.hh"
#include "args.hh"

namespace nix {

void Config::set(const std::string & name, const std::string & value)
{
    auto i = _settings.find(name);
    if (i == _settings.end())
        throw UsageError("unknown setting '%s'", name);
    i->second.setting->set(value);
}

void Config::addSetting(AbstractSetting * setting)
{
    _settings.emplace(setting->name, Config::SettingData{false, setting});
    for (auto & alias : setting->aliases)
        _settings.emplace(alias, Config::SettingData{true, setting});

    bool set = false;

    auto i = initials.find(setting->name);
    if (i != initials.end()) {
        setting->set(i->second);
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

StringMap Config::getSettings()
{
    StringMap res;
    for (auto & opt : _settings)
        if (!opt.second.isAlias)
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
                throw UsageError("illegal configuration line ‘%1%’ in ‘%2%’", line, path);

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

AbstractSetting::AbstractSetting(
    const std::string & name,
    const std::string & description,
    const std::set<std::string> & aliases)
    : name(name), description(description), aliases(aliases)
{
}

template<> void Setting<std::string>::set(const std::string & str)
{
    value = str;
}

template<> std::string Setting<std::string>::to_string()
{
    return value;
}

template<typename T, typename Tag>
void Setting<T, Tag>::set(const std::string & str)
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    if (!string2Int(str, value))
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
}

template<typename T, typename Tag>
std::string Setting<T, Tag>::to_string()
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    return std::to_string(value);
}

bool AbstractSetting::parseBool(const std::string & str)
{
    if (str == "true" || str == "yes" || str == "1")
        return true;
    else if (str == "false" || str == "no" || str == "0")
        return false;
    else
        throw UsageError("Boolean setting '%s' has invalid value '%s'", name, str);
}

template<> void Setting<bool>::set(const std::string & str)
{
    value = parseBool(str);
}

std::string AbstractSetting::printBool(bool b)
{
    return b ? "true" : "false";
}


template<> std::string Setting<bool>::to_string()
{
    return printBool(value);
}

template<> void Setting<Strings>::set(const std::string & str)
{
    value = tokenizeString<Strings>(str);
}

template<> std::string Setting<Strings>::to_string()
{
    return concatStringsSep(" ", value);
}

template<> void Setting<StringSet>::set(const std::string & str)
{
    value = tokenizeString<StringSet>(str);
}

template<> std::string Setting<StringSet>::to_string()
{
    return concatStringsSep(" ", value);
}

template class Setting<int>;
template class Setting<unsigned int>;
template class Setting<long>;
template class Setting<unsigned long>;
template class Setting<long long>;
template class Setting<unsigned long long>;

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
