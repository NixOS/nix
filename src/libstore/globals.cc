#include "globals.hh"

#include <map>


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixStateDir = "/UNINIT";
string nixDBPath = "/UNINIT";
string nixConfDir = "/UNINIT";

bool keepFailed = false;

bool keepGoing = false;

bool tryFallback = false;

Verbosity buildVerbosity = lvlInfo;

unsigned int maxBuildJobs = 1;

bool readOnlyMode = false;


static bool settingsRead = false;

static map<string, string> settings;


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

        unsigned int hash = line.find('#');
        if (hash != string::npos)
            line = string(line, 0, hash);

        if (line.find_first_not_of(" ") == string::npos) continue;

        istringstream is(line);
        string name, sep, value;
        is >> name >> sep >> value;
        if (sep != "=" || !is)
            throw Error(format("illegal configuration line `%1%' in `%2%'") % line % settingsFile);
        
        settings[name] = value;
    };
    
    settingsRead = true;
}


string querySetting(const string & name, const string & def)
{
    if (!settingsRead) readSettings();
    map<string, string>::iterator i = settings.find(name);
    return i == settings.end() ? def : i->second;
}


bool queryBoolSetting(const string & name, bool def)
{
    string value = querySetting(name, def ? "true" : "false");
    if (value == "true") return true;
    else if (value == "false") return false;
    else throw Error(format("configuration option `%1%' should be either `true' or `false', not `%2%'")
        % name % value);
}
