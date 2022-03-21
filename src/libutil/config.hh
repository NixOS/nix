#include <cassert>
#include <map>
#include <set>

#include "types.hh"

#include <nlohmann/json_fwd.hpp>

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
    virtual bool set(const std::string & name, const std::string & value) = 0;

    struct SettingInfo
    {
        std::string value;
        std::string description;
    };

    /**
     * Adds the currently known settings to the given result map `res`.
     * - res: map to store settings in
     * - overriddenOnly: when set to true only overridden settings will be added to `res`
     */
    virtual void getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly = false) = 0;

    /**
     * Parses the configuration in `contents` and applies it
     * - contents: configuration contents to be parsed and applied
     * - path: location of the configuration file
     */
    void applyConfig(const std::string & contents, const std::string & path = "<unknown>");

    /**
     * Applies a nix configuration file
     * - path: the location of the config file to apply
     */
    void applyConfigFile(const Path & path);

    /**
     * Resets the `overridden` flag of all Settings
     */
    virtual void resetOverridden() = 0;

    /**
     * Outputs all settings to JSON
     * - out: JSONObject to write the configuration to
     */
    virtual nlohmann::json toJSON() = 0;

    /**
     * Outputs all settings in a key-value pair format suitable to be used as
     * `nix.conf`
     */
    virtual std::string toKeyValue() = 0;

    /**
     * Converts settings to `Args` to be used on the command line interface
     * - args: args to write to
     * - category: category of the settings
     */
    virtual void convertToArgs(Args & args, const std::string & category) = 0;

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
       std::cout << foo << "\n"; // will print 123 unless overridden
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

    typedef std::map<std::string, SettingData> Settings;

private:

    Settings _settings;

public:

    Config(const StringMap & initials = {})
        : AbstractConfig(initials)
    { }

    bool set(const std::string & name, const std::string & value) override;

    void addSetting(AbstractSetting * setting);

    void getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly = false) override;

    void resetOverridden() override;

    nlohmann::json toJSON() override;

    std::string toKeyValue() override;

    void convertToArgs(Args & args, const std::string & category) override;
};

class AbstractSetting
{
    friend class Config;

public:

    const std::string name;
    const std::string description;
    const std::set<std::string> aliases;

    int created = 123;

    bool overridden = false;

protected:

    AbstractSetting(
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases);

    virtual ~AbstractSetting()
    {
        // Check against a gcc miscompilation causing our constructor
        // not to run (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80431).
        assert(created == 123);
    }

    virtual void set(const std::string & value, bool append = false) = 0;

    virtual bool isAppendable()
    { return false; }

    virtual std::string to_string() const = 0;

    nlohmann::json toJSON();

    virtual std::map<std::string, nlohmann::json> toJSONObject();

    virtual void convertToArg(Args & args, const std::string & category);

    bool isOverridden() const { return overridden; }
};

/* A setting of type T. */
template<typename T>
class BaseSetting : public AbstractSetting
{
protected:

    T value;
    const T defaultValue;
    const bool documentDefault;

public:

    BaseSetting(const T & def,
        const bool documentDefault,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : AbstractSetting(name, description, aliases)
        , value(def)
        , defaultValue(def)
        , documentDefault(documentDefault)
    { }

    operator const T &() const { return value; }
    operator T &() { return value; }
    const T & get() const { return value; }
    bool operator ==(const T & v2) const { return value == v2; }
    bool operator !=(const T & v2) const { return value != v2; }
    void operator =(const T & v) { assign(v); }
    virtual void assign(const T & v) { value = v; }
    void setDefault(const T & v) { if (!overridden) value = v; }

    void set(const std::string & str, bool append = false) override;

    bool isAppendable() override;

    virtual void override(const T & v)
    {
        overridden = true;
        value = v;
    }

    std::string to_string() const override;

    void convertToArg(Args & args, const std::string & category) override;

    std::map<std::string, nlohmann::json> toJSONObject() override;
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
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {},
        const bool documentDefault = true)
        : BaseSetting<T>(def, documentDefault, name, description, aliases)
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
        const Path & def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : BaseSetting<Path>(def, true, name, description, aliases)
        , allowEmpty(allowEmpty)
    {
        options->addSetting(this);
    }

    void set(const std::string & str, bool append = false) override;

    Path operator +(const char * p) const { return value + p; }

    void operator =(const Path & v) { this->assign(v); }
};

struct GlobalConfig : public AbstractConfig
{
    typedef std::vector<Config*> ConfigRegistrations;
    static ConfigRegistrations * configRegistrations;

    bool set(const std::string & name, const std::string & value) override;

    void getSettings(std::map<std::string, SettingInfo> & res, bool overriddenOnly = false) override;

    void resetOverridden() override;

    nlohmann::json toJSON() override;

    std::string toKeyValue() override;

    void convertToArgs(Args & args, const std::string & category) override;

    struct Register
    {
        Register(Config * config);
    };
};

extern GlobalConfig globalConfig;

}
