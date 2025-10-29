#include "nix/util/configuration.hh"
#include "nix/util/args.hh"
#include "nix/util/abstract-setting-to-json.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/util.hh"
#include "nix/util/file-system.hh"

#include "nix/util/config-impl.hh"

#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

namespace nix {

Config::Config(StringMap initials)
    : AbstractConfig(std::move(initials))
{
}

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
    _settings.emplace(setting->name, Config::SettingData{false, setting});
    for (const auto & alias : setting->aliases)
        _settings.emplace(alias, Config::SettingData{true, setting});

    bool set = false;

    if (auto i = unknownSettings.find(setting->name); i != unknownSettings.end()) {
        setting->set(std::move(i->second));
        setting->overridden = true;
        unknownSettings.erase(i);
        set = true;
    }

    for (auto & alias : setting->aliases) {
        if (auto i = unknownSettings.find(alias); i != unknownSettings.end()) {
            if (set)
                warn("setting '%s' is set, but it's an alias of '%s' which is also set", alias, setting->name);
            else {
                setting->set(std::move(i->second));
                setting->overridden = true;
                unknownSettings.erase(i);
                set = true;
            }
        }
    }
}

AbstractConfig::AbstractConfig(StringMap initials)
    : unknownSettings(std::move(initials))
{
}

void AbstractConfig::warnUnknownSettings()
{
    for (const auto & s : unknownSettings)
        warn("unknown setting '%s'", s.first);
}

void AbstractConfig::reapplyUnknownSettings()
{
    auto unknownSettings2 = std::move(unknownSettings);
    unknownSettings = {};
    for (auto & s : unknownSettings2)
        set(s.first, s.second);
}

void Config::getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly) const
{
    for (const auto & opt : _settings)
        if (!opt.second.isAlias && (!overriddenOnly || opt.second.setting->overridden)
            && experimentalFeatureSettings.isEnabled(opt.second.setting->experimentalFeature))
            res.emplace(opt.first, SettingInfo{opt.second.setting->to_string(), opt.second.setting->description});
}

/**
 * Parse configuration in `contents`, and also the configuration files included from there, with their location
 * specified relative to `path`.
 *
 * `contents` and `path` represent the file that is being parsed.
 * The result is only an intermediate list of key-value pairs of strings.
 * More parsing according to the settings-specific semantics is being done by `loadConfFile` in `libstore/globals.cc`.
 */
static void parseConfigFiles(
    const std::string & contents,
    const std::string & path,
    std::vector<std::pair<std::string, std::string>> & parsedContents)
{
    unsigned int pos = 0;

    while (pos < contents.size()) {
        std::string line;
        while (pos < contents.size() && contents[pos] != '\n')
            line += contents[pos++];
        pos++;

        if (auto hash = line.find('#'); hash != line.npos)
            line = std::string(line, 0, hash);

        auto tokens = tokenizeString<std::vector<std::string>>(line);
        if (tokens.empty())
            continue;

        if (tokens.size() < 2)
            throw UsageError("syntax error in configuration line '%1%' in '%2%'", line, path);

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
                throw UsageError("syntax error in configuration line '%1%' in '%2%'", line, path);
            auto p = absPath(tokens[1], dirOf(path));
            if (pathExists(p)) {
                try {
                    std::string includedContents = readFile(p);
                    parseConfigFiles(includedContents, p, parsedContents);
                } catch (SystemError &) {
                    // TODO: Do we actually want to ignore this? Or is it better to fail?
                }
            } else if (!ignoreMissing) {
                throw Error("file '%1%' included from '%2%' not found", p, path);
            }
            continue;
        }

        if (tokens[1] != "=")
            throw UsageError("syntax error in configuration line '%1%' in '%2%'", line, path);

        std::string name = std::move(tokens[0]);

        auto i = tokens.begin();
        advance(i, 2);

        parsedContents.push_back({
            std::move(name),
            concatStringsSep(" ", Strings(i, tokens.end())),
        });
    };
}

void AbstractConfig::applyConfig(const std::string & contents, const std::string & path)
{
    std::vector<std::pair<std::string, std::string>> parsedContents;

    parseConfigFiles(contents, path, parsedContents);

    // First apply experimental-feature related settings
    for (const auto & [name, value] : parsedContents)
        if (name == "experimental-features" || name == "extra-experimental-features")
            set(name, value);

    // Then apply other settings
    // XXX: NIX_PATH must override the regular setting! This is done in `initGC()`
    // Environment variables overriding settings should probably be part of the Config mechanism,
    // but at the time of writing it's not worth building that for just one thing
    for (const auto & [name, value] : parsedContents) {
        if (name != "experimental-features" && name != "extra-experimental-features") {
            if ((name == "nix-path" || name == "extra-nix-path") && getEnv("NIX_PATH").has_value()) {
                continue;
            }
            set(name, value);
        }
    }
}

void Config::resetOverridden()
{
    for (auto & s : _settings)
        s.second.setting->overridden = false;
}

nlohmann::json Config::toJSON()
{
    auto res = nlohmann::json::object();
    for (const auto & s : _settings)
        if (!s.second.isAlias)
            res.emplace(s.first, s.second.setting->toJSON());
    return res;
}

std::string Config::toKeyValue()
{
    std::string res;
    for (const auto & s : _settings)
        if (s.second.isAlias)
            res += fmt("%s = %s\n", s.first, s.second.setting->to_string());
    return res;
}

void Config::convertToArgs(Args & args, const std::string & category)
{
    for (auto & s : _settings) {
        if (!s.second.isAlias)
            s.second.setting->convertToArg(args, category);
    }
}

AbstractSetting::AbstractSetting(
    const std::string & name,
    const std::string & description,
    const StringSet & aliases,
    std::optional<ExperimentalFeature> experimentalFeature)
    : name(name)
    , description(stripIndentation(description))
    , aliases(aliases)
    , experimentalFeature(std::move(experimentalFeature))
{
}

AbstractSetting::~AbstractSetting()
{
    // Check against a gcc miscompilation causing our constructor
    // not to run (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80431).
    assert(created == 123);
}

nlohmann::json AbstractSetting::toJSON()
{
    return nlohmann::json(toJSONObject());
}

std::map<std::string, nlohmann::json> AbstractSetting::toJSONObject() const
{
    std::map<std::string, nlohmann::json> obj;
    obj.emplace("description", description);
    obj.emplace("aliases", aliases);
    if (experimentalFeature)
        obj.emplace("experimentalFeature", *experimentalFeature);
    else
        obj.emplace("experimentalFeature", nullptr);
    return obj;
}

void AbstractSetting::convertToArg(Args & args, const std::string & category) {}

bool AbstractSetting::isOverridden() const
{
    return overridden;
}

template<>
std::string BaseSetting<std::string>::parse(const std::string & str) const
{
    return str;
}

template<>
std::string BaseSetting<std::string>::to_string() const
{
    return value;
}

template<>
std::optional<std::string> BaseSetting<std::optional<std::string>>::parse(const std::string & str) const
{
    if (str == "")
        return std::nullopt;
    else
        return {str};
}

template<>
std::string BaseSetting<std::optional<std::string>>::to_string() const
{
    return value ? *value : "";
}

template<>
bool BaseSetting<bool>::parse(const std::string & str) const
{
    if (str == "true" || str == "yes" || str == "1")
        return true;
    else if (str == "false" || str == "no" || str == "0")
        return false;
    else
        throw UsageError("Boolean setting '%s' has invalid value '%s'", name, str);
}

template<>
std::string BaseSetting<bool>::to_string() const
{
    return value ? "true" : "false";
}

template<>
void BaseSetting<bool>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .aliases = aliases,
        .description = fmt("Enable the `%s` setting.", name),
        .category = category,
        .handler = {[this] { override(true); }},
        .experimentalFeature = experimentalFeature,
    });
    args.addFlag({
        .longName = "no-" + name,
        .aliases = aliases,
        .description = fmt("Disable the `%s` setting.", name),
        .category = category,
        .handler = {[this] { override(false); }},
        .experimentalFeature = experimentalFeature,
    });
}

template<>
Strings BaseSetting<Strings>::parse(const std::string & str) const
{
    return tokenizeString<Strings>(str);
}

template<>
void BaseSetting<Strings>::appendOrSet(Strings newValue, bool append)
{
    if (!append)
        value.clear();
    value.insert(value.end(), std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
}

template<>
std::string BaseSetting<Strings>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<>
StringSet BaseSetting<StringSet>::parse(const std::string & str) const
{
    return tokenizeString<StringSet>(str);
}

template<>
void BaseSetting<StringSet>::appendOrSet(StringSet newValue, bool append)
{
    if (!append)
        value.clear();
    value.insert(std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
}

template<>
std::string BaseSetting<StringSet>::to_string() const
{
    return concatStringsSep(" ", value);
}

template<>
std::set<ExperimentalFeature> BaseSetting<std::set<ExperimentalFeature>>::parse(const std::string & str) const
{
    std::set<ExperimentalFeature> res;
    for (auto & s : tokenizeString<StringSet>(str)) {
        if (auto thisXpFeature = parseExperimentalFeature(s); thisXpFeature) {
            res.insert(thisXpFeature.value());
            if (thisXpFeature.value() == Xp::Flakes)
                res.insert(Xp::FetchTree);
        } else
            warn("unknown experimental feature '%s'", s);
    }
    return res;
}

template<>
void BaseSetting<std::set<ExperimentalFeature>>::appendOrSet(std::set<ExperimentalFeature> newValue, bool append)
{
    if (!append)
        value.clear();
    value.insert(std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
}

template<>
std::string BaseSetting<std::set<ExperimentalFeature>>::to_string() const
{
    StringSet stringifiedXpFeatures;
    for (const auto & feature : value)
        stringifiedXpFeatures.insert(std::string(showExperimentalFeature(feature)));
    return concatStringsSep(" ", stringifiedXpFeatures);
}

template<>
StringMap BaseSetting<StringMap>::parse(const std::string & str) const
{
    StringMap res;
    for (const auto & s : tokenizeString<Strings>(str)) {
        if (auto eq = s.find_first_of('='); s.npos != eq)
            res.emplace(std::string(s, 0, eq), std::string(s, eq + 1));
        // else ignored
    }
    return res;
}

template<>
void BaseSetting<StringMap>::appendOrSet(StringMap newValue, bool append)
{
    if (!append)
        value.clear();
    value.insert(std::make_move_iterator(newValue.begin()), std::make_move_iterator(newValue.end()));
}

template<>
std::string BaseSetting<StringMap>::to_string() const
{
    return std::transform_reduce(
        value.cbegin(),
        value.cend(),
        std::string{},
        [](const auto & l, const auto & r) { return l + " " + r; },
        [](const auto & kvpair) { return kvpair.first + "=" + kvpair.second; });
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

static Path parsePath(const AbstractSetting & s, const std::string & str)
{
    if (str == "")
        throw UsageError("setting '%s' is a path and paths cannot be empty", s.name);
    else
        return canonPath(str);
}

PathSetting::PathSetting(
    Config * options,
    const Path & def,
    const std::string & name,
    const std::string & description,
    const StringSet & aliases)
    : BaseSetting<Path>(def, true, name, description, aliases)
{
    options->addSetting(this);
}

Path PathSetting::parse(const std::string & str) const
{
    return parsePath(*this, str);
}

OptionalPathSetting::OptionalPathSetting(
    Config * options,
    const std::optional<Path> & def,
    const std::string & name,
    const std::string & description,
    const StringSet & aliases)
    : BaseSetting<std::optional<Path>>(def, true, name, description, aliases)
{
    options->addSetting(this);
}

std::optional<Path> OptionalPathSetting::parse(const std::string & str) const
{
    if (str == "")
        return std::nullopt;
    else
        return parsePath(*this, str);
}

void OptionalPathSetting::operator=(const std::optional<Path> & v)
{
    this->assign(v);
}

bool ExperimentalFeatureSettings::isEnabled(const ExperimentalFeature & feature) const
{
    auto & f = experimentalFeatures.get();
    return std::find(f.begin(), f.end(), feature) != f.end();
}

void ExperimentalFeatureSettings::require(const ExperimentalFeature & feature, std::string reason) const
{
    if (!isEnabled(feature))
        throw MissingExperimentalFeature(feature, std::move(reason));
}

bool ExperimentalFeatureSettings::isEnabled(const std::optional<ExperimentalFeature> & feature) const
{
    return !feature || isEnabled(*feature);
}

void ExperimentalFeatureSettings::require(const std::optional<ExperimentalFeature> & feature) const
{
    if (feature)
        require(*feature);
}

} // namespace nix
