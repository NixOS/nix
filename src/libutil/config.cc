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

void Config::add(AbstractSetting * setting)
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

void Config::warnUnused()
{
    for (auto & i : initials)
        warn("unknown setting '%s'", i.first);
}

std::string Config::dump()
{
    std::string res;
    for (auto & opt : _settings)
        if (!opt.second.isAlias)
            res += opt.first + " = " + opt.second.setting->to_string() + "\n";
    return res;
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

template<typename T>
void Setting<T>::set(const std::string & str)
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    try {
        auto i = std::stoll(str);
        if (i < std::numeric_limits<T>::min() ||
            i > std::numeric_limits<T>::max())
            throw UsageError("setting '%s' has out-of-range value %d", name, i);
        value = i;
    } catch (std::logic_error&) {
        throw UsageError("setting '%s' has invalid value '%s'", name, str);
    }
}

template<typename T>
std::string Setting<T>::to_string()
{
    static_assert(std::is_integral<T>::value, "Integer required.");
    return std::to_string(value);
}

template<> void Setting<bool>::set(const std::string & str)
{
    if (str == "true" || str == "yes" || str == "1")
        value = true;
    else if (str == "false" || str == "no" || str == "0")
        value = false;
    else
        throw UsageError("Boolean setting '%s' has invalid value '%s'", name, str);
}

template<> std::string Setting<bool>::to_string()
{
    return value ? "true" : "false";
}

template class Setting<int>;
template class Setting<unsigned int>;
template class Setting<long>;
template class Setting<size_t>;

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
