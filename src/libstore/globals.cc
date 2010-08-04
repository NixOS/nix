#include "globals.hh"
#include "util.hh"

#include <map>
#include <algorithm>


namespace nix {


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixStateDir = "/UNINIT";
string nixDBPath = "/UNINIT";
string nixConfDir = "/UNINIT";
string nixLibexecDir = "/UNINIT";
string nixBinDir = "/UNINIT";

bool keepFailed = false;
bool keepGoing = false;
bool tryFallback = false;
Verbosity buildVerbosity = lvlInfo;
unsigned int maxBuildJobs = 1;
unsigned int buildCores = 1;
bool readOnlyMode = false;
string thisSystem = "unset";
time_t maxSilentTime = 0;
Paths substituters;
bool useBuildHook = true;
bool printBuildTrace = false;


static bool settingsRead = false;

static std::map<string, Strings> settings;

/* Overriden settings. */
std::map<string, Strings> settingsCmdline;


string & at(Strings & ss, unsigned int n)
{
    Strings::iterator i = ss.begin();
    advance(i, n);
    return *i;
}


static void readSettings()
{
    Path settingsFile = (format("%1%/%2%") % nixConfDir % "nix.conf").str();
    if (!pathExists(settingsFile)) return;
    string contents = readFile(settingsFile);

    unsigned int pos = 0;

    while (pos < contents.size()) {
        string line;
        while (pos < contents.size() && contents[pos] != '\n')
            line += contents[pos++];
        pos++;

        string::size_type hash = line.find('#');
        if (hash != string::npos)
            line = string(line, 0, hash);

        Strings tokens = tokenizeString(line);
        if (tokens.empty()) continue;

        if (tokens.size() < 2 || at(tokens, 1) != "=")
            throw Error(format("illegal configuration line `%1%' in `%2%'") % line % settingsFile);

        string name = at(tokens, 0);

        Strings::iterator i = tokens.begin();
        advance(i, 2);
        settings[name] = Strings(i, tokens.end());
    };

    settings.insert(settingsCmdline.begin(), settingsCmdline.end());
    
    settingsRead = true;
}


Strings querySetting(const string & name, const Strings & def)
{
    if (!settingsRead) readSettings();
    std::map<string, Strings>::iterator i = settings.find(name);
    return i == settings.end() ? def : i->second;
}


string querySetting(const string & name, const string & def)
{
    Strings defs;
    defs.push_back(def);

    Strings value = querySetting(name, defs);
    if (value.size() != 1)
        throw Error(format("configuration option `%1%' should not be a list") % name);

    return value.front();
}


bool queryBoolSetting(const string & name, bool def)
{
    string v = querySetting(name, def ? "true" : "false");
    if (v == "true") return true;
    else if (v == "false") return false;
    else throw Error(format("configuration option `%1%' should be either `true' or `false', not `%2%'")
        % name % v);
}


unsigned int queryIntSetting(const string & name, unsigned int def)
{
    int n;
    if (!string2Int(querySetting(name, int2String(def)), n) || n < 0)
        throw Error(format("configuration setting `%1%' should have an integer value") % name);
    return n;
}


void overrideSetting(const string & name, const Strings & value)
{
    if (settingsRead) settings[name] = value;
    settingsCmdline[name] = value;
}


void reloadSettings()
{
    settingsRead = false;
    settings.clear();
}

 
}
