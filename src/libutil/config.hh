#include <cassert>
#include <map>
#include <set>

#include "types.hh"

#pragma once

namespace nix {

/**
 * The Config class provides Nix runtime configurations.
 *
 * What is a Configuration?
 *   A collection of uniquely named Settings.
 *
 * What is a Setting?
 *   Each property that you can set in a configuration corresponds to a
 *   `Setting`. A setting records value and description of a property
 *   with a default and optional aliases.
 *
 * A valid configuration consists of settings that are registered to a
 * `Config` object instance:
 *
 *   Config config;
 *   Setting<std::string> systemSetting{&config, "x86_64-linux", "system", "the current system"};
 *
 * The above creates a `Config` object and registers a setting called "system"
 * via the variable `systemSetting` with it. The setting defaults to the string
 * "x86_64-linux", it's description is "the current system". All of the
 * registered settings can then be accessed as shown below:
 *
 *   std::map<std::string, Config::SettingInfo> settings;
 *   config.getSettings(settings);
 *   config["system"].description == "the current system"
 *   config["system"].value == "x86_64-linux"
 *
 *
 * The above retrieves all currently known settings from the `Config` object
 * and adds them to the `settings` map.
 */

class Args;
class AbstractSetting;
class JSONPlaceholder;
class JSONObject;

class AbstractConfig
{
protected:
    StringMap unknownSettings;

    AbstractConfig(const StringMap & initials = {})
        : unknownSettings(initials)
    { }

public:

    /**
     * Sets the value referenced by `name` to `value`. Returns true if the
     * setting is known, false otherwise.
     */
    virtual bool set(std::string_view name, std::string_view value) = 0;

    struct SettingInfo
    {
        std::string value;
        std::string description;
    };

    /**
     * Adds the currently known settings to the given result map `res`.
     * - res: map to store settings in
     * - overridenOnly: when set to true only overridden settings will be added to `res`
     */
    virtual void getSettings(std::map<std::string, SettingInfo> & res, bool overridenOnly = false) = 0;

    /**
     * Parses the configuration in `contents` and applies it
     * - contents: configuration contents to be parsed and applied
     * - path: location of the configuration file
     */
    void applyConfig(std::string_view contents, std::string_view path = "<unknown>");

    /**
     * Applies a nix configuration file
     * - path: the location of the config file to apply
     */
    void applyConfigFile(PathView path);

    /**
     * Resets the `overridden` flag of all Settings
     */
    virtual void resetOverriden() = 0;

    /**
     * Outputs all settings to JSON
     * - out: JSONObject to write the configuration to
     */
    virtual void toJSON(JSONObject & out) = 0;

    /**
     * Converts settings to `Args` to be used on the command line interface
     * - args: args to write to
     * - category: category of the settings
     */
    virtual void convertToArgs(Args & args, std::string_view category) = 0;

    /**
     * Logs a warning for each unregistered setting
     */
    void warnUnknownSettings();

    /**
     * Re-applies all previously attempted changes to unknown settings
     */
    void reapplyUnknownSettings();
};

/* A class to simplify providing configuration settings. The typical
   use is to inherit Config and add Setting<T> members:

   class MyClass : private Config
   {
     Setting<int> foo{this, 123, "foo", "the number of foos to use"};
     Setting<std::string> bar{this, "blabla", "bar", "the name of the bar"};

     MyClass() : Config(readConfigFile("/etc/my-app.conf"))
     {
       std::cout << foo << "\n"; // will print 123 unless overriden
     }
   };
*/

class Config : public AbstractConfig
{
    friend class AbstractSetting;

public:

    struct SettingData
    {
        bool isAlias;
        AbstractSetting * setting;
        SettingData(bool isAlias, AbstractSetting * setting)
            : isAlias(isAlias), setting(setting)
        { }
    };

    typedef std::map<std::string, SettingData, std::less<>> Settings;

private:

    Settings _settings;

public:

    Config(const StringMap & initials = {})
        : AbstractConfig(initials)
    { }

    bool set(std::string_view name, std::string_view value) override;

    void addSetting(AbstractSetting * setting);

    void getSettings(std::map<std::string, SettingInfo> & res, bool overridenOnly = false) override;

    void resetOverriden() override;

    void toJSON(JSONObject & out) override;

    void convertToArgs(Args & args, std::string_view category) override;
};

class AbstractSetting
{
    friend class Config;

public:

    const std::string name;
    const std::string description;
    const std::set<std::string> aliases;

    int created = 123;

    bool overriden = false;

    void setDefault(std::string_view str);

protected:

    AbstractSetting(
        std::string_view name,
        std::string_view description,
        const std::set<std::string> & aliases);

    virtual ~AbstractSetting()
    {
        // Check against a gcc miscompilation causing our constructor
        // not to run (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80431).
        assert(created == 123);
    }

    virtual void set(std::string_view value) = 0;

    virtual std::string to_string() const = 0;

    virtual void toJSON(JSONPlaceholder & out);

    virtual void convertToArg(Args & args, std::string_view category);

    bool isOverriden() const { return overriden; }
};

// To make class template specialization less annoying
#define MAKE_BASE_SETTING(T) \
protected: \
\
    T value; \
\
public: \
\
    BaseSetting(const T & def, \
        std::string_view name, \
        std::string_view description, \
        const std::set<std::string> & aliases = {}) \
        : AbstractSetting(name, description, aliases) \
        , value(def) \
    { } \
\
    operator const T &() const { return value; } \
    operator T &() { return value; } \
    const T & get() const { return value; } \
    bool operator ==(const T & v2) const { return value == v2; } \
    bool operator !=(const T & v2) const { return value != v2; } \
    void operator =(const T & v) { assign(v); } \
    virtual void assign(const T & v) { value = v; } \
\
    void set(std::string_view str) override; \
\
    virtual void override(const T & v) \
    { \
        overriden = true; \
        value = v; \
    } \
\
    std::string to_string() const override; \
\
    void convertToArg(Args & args, std::string_view category) override; \
\
    void toJSON(JSONPlaceholder & out) override;

/* A setting of type T. */
template<typename T>
class BaseSetting : public AbstractSetting
{
    MAKE_BASE_SETTING(T)
};

template<>
class BaseSetting<std::string> : public AbstractSetting
{
    MAKE_BASE_SETTING(std::string)
    operator std::string_view () const { return std::string_view { value }; }
};

template<typename T>
std::ostream & operator <<(std::ostream & str, const BaseSetting<T> & opt)
{
    str << (const T &) opt;
    return str;
}

template<typename T>
bool operator ==(const T & v1, const BaseSetting<T> & v2) { return v1 == (const T &) v2; }

template<typename T>
class Setting : public BaseSetting<T>
{
public:
    Setting(Config * options,
        const T & def,
        std::string_view name,
        std::string_view description,
        const std::set<std::string> & aliases = {})
        : BaseSetting<T>(def, name, description, aliases)
    {
        options->addSetting(this);
    }

    void operator =(const T & v) { this->assign(v); }
};

/* A special setting for Paths. These are automatically canonicalised
   (e.g. "/foo//bar/" becomes "/foo/bar"). */
class PathSetting : public BaseSetting<Path>
{
    bool allowEmpty;

public:

    PathSetting(Config * options,
        bool allowEmpty,
        PathView def,
        std::string_view name,
        std::string_view description,
        const std::set<std::string> & aliases = {})
        : BaseSetting<Path>(Path { def }, name, description, aliases)
        , allowEmpty(allowEmpty)
    {
        options->addSetting(this);
    }

    void set(std::string_view str) override;

    Path operator +(const char * p) const { return value + p; }

    void operator =(PathView v) { this->assign(Path { v }); }
};

struct GlobalConfig : public AbstractConfig
{
    typedef std::vector<Config*> ConfigRegistrations;
    static ConfigRegistrations * configRegistrations;

    bool set(std::string_view name, std::string_view value) override;

    void getSettings(std::map<std::string, SettingInfo> & res, bool overridenOnly = false) override;

    void resetOverriden() override;

    void toJSON(JSONObject & out) override;

    void convertToArgs(Args & args, std::string_view category) override;

    struct Register
    {
        Register(Config * config);
    };
};

extern GlobalConfig globalConfig;

}
