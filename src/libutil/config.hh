#include <map>
#include <set>

#include "types.hh"

#pragma once

namespace nix {

class Args;
class AbstractSetting;
class JSONPlaceholder;
class JSONObject;

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

class Config
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

    StringMap extras;

public:

    Config(const StringMap & initials)
        : extras(initials)
    { }

    void set(const std::string & name, const std::string & value);

    void addSetting(AbstractSetting * setting);

    void handleUnknownSettings();

    enum SettingsSubset {
        all,
        overriddenOnly,
        forwardableOnly
    };

    StringMap getSettings(SettingsSubset subset = all);

    const Settings & _getSettings() { return _settings; }

    void applyConfigFile(const Path & path);

    void resetOverriden();

    void toJSON(JSONObject & out);

    void convertToArgs(Args & args, const std::string & category);
};

class AbstractSetting
{
    friend class Config;

public:

    const std::string name;
    const std::string description;
    const std::set<std::string> aliases;
    /* Whether the setting should be forwarded to remote machines. */
    const bool forwardable;

    int created = 123;

    bool overriden = false;

protected:

    AbstractSetting(
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases,
        bool forwardable = false);

    virtual ~AbstractSetting()
    {
        // Check against a gcc miscompilation causing our constructor
        // not to run (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80431).
        assert(created == 123);
    }

    virtual void set(const std::string & value) = 0;

    virtual std::string to_string() = 0;

    virtual void toJSON(JSONPlaceholder & out);

    virtual void convertToArg(Args & args, const std::string & category);

    bool isOverriden() { return overriden; }
};

/* A setting of type T. */
template<typename T>
class BaseSetting : public AbstractSetting
{
protected:

    T value;

public:

    BaseSetting(const T & def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {},
        bool forwardable = false)
        : AbstractSetting(name, description, aliases, forwardable)
        , value(def)
    { }

    operator const T &() const { return value; }
    operator T &() { return value; }
    const T & get() const { return value; }
    bool operator ==(const T & v2) const { return value == v2; }
    bool operator !=(const T & v2) const { return value != v2; }
    void operator =(const T & v) { assign(v); }
    virtual void assign(const T & v) { value = v; }

    void set(const std::string & str) override;

    virtual void override(const T & v)
    {
        overriden = true;
        value = v;
    }

    std::string to_string() override;

    void convertToArg(Args & args, const std::string & category) override;

    void toJSON(JSONPlaceholder & out) override;
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
        bool forwardable = false)
        : BaseSetting<T>(def, name, description, aliases, forwardable)
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
        const std::set<std::string> & aliases = {},
        bool forwardable = false)
        : BaseSetting<Path>(def, name, description, aliases, forwardable)
        , allowEmpty(allowEmpty)
    {
        options->addSetting(this);
    }

    void set(const std::string & str) override;

    Path operator +(const char * p) const { return value + p; }

    void operator =(const Path & v) { this->assign(v); }
};

}
