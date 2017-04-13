#include <map>
#include <set>

#include "types.hh"

#pragma once

namespace nix {

class Args;
class AbstractSetting;

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

    struct SettingData
    {
        bool isAlias = false;
        AbstractSetting * setting;
    };

    std::map<std::string, SettingData> _settings;

    StringMap initials;

public:

    Config(const StringMap & initials)
        : initials(initials)
    { }

    void set(const std::string & name, const std::string & value);

    void addSetting(AbstractSetting * setting);

    void warnUnknownSettings();

    StringMap getSettings();

    void applyConfigFile(const Path & path, bool fatal = false);
};

class AbstractSetting
{
    friend class Config;

public:

    const std::string name;
    const std::string description;
    const std::set<std::string> aliases;

    int created = 123;

protected:

    AbstractSetting(
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases);

    virtual ~AbstractSetting()
    {
        // Check against a gcc miscompilation causing our constructor
        // not to run.
        assert(created == 123);
    }

    virtual void set(const std::string & value) = 0;

    virtual std::string to_string() = 0;

    bool parseBool(const std::string & str);
    std::string printBool(bool b);
};

struct DefaultSettingTag { };

/* A setting of type T. */
template<typename T, typename Tag = DefaultSettingTag>
class Setting : public AbstractSetting
{
protected:

    T value;

public:

    Setting(Config * options,
        const T & def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : AbstractSetting(name, description, aliases)
        , value(def)
    {
        options->addSetting(this);
    }

    operator const T &() const { return value; }
    operator T &() { return value; }
    const T & get() const { return value; }
    bool operator ==(const T & v2) const { return value == v2; }
    bool operator !=(const T & v2) const { return value != v2; }
    void operator =(const T & v) { value = v; }

    void set(const std::string & str) override;

    std::string to_string() override;
};

template<typename T>
std::ostream & operator <<(std::ostream & str, const Setting<T> & opt)
{
    str << (const T &) opt;
    return str;
}

template<typename T>
bool operator ==(const T & v1, const Setting<T> & v2) { return v1 == (const T &) v2; }

/* A special setting for Paths. These are automatically canonicalised
   (e.g. "/foo//bar/" becomes "/foo/bar"). */
class PathSetting : public Setting<Path>
{
    bool allowEmpty;

public:

    PathSetting(Config * options,
        bool allowEmpty,
        const Path & def,
        const std::string & name,
        const std::string & description,
        const std::set<std::string> & aliases = {})
        : Setting<Path>(options, def, name, description, aliases)
        , allowEmpty(allowEmpty)
    {
        set(value);
    }

    void set(const std::string & str) override;

    Path operator +(const char * p) const { return value + p; }
};

}
