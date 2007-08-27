#include "globals.hh"
#include "util.hh"

#include <map>
#include <algorithm>
#include <pwd.h>

namespace nix {


string nixStore = "/UNINIT";
string nixStoreState = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixStateDir = "/UNINIT";
string nixDBPath = "/UNINIT";
string nixExt3CowHeader = "/UNINIT";
string nixConfDir = "/UNINIT";
string nixLibexecDir = "/UNINIT";
string nixBinDir = "/UNINIT";

bool keepFailed = false;
bool keepGoing = false;
bool tryFallback = false;
Verbosity buildVerbosity = lvlInfo;
unsigned int maxBuildJobs = 1;
bool readOnlyMode = false;
string thisSystem = "unset";
unsigned int maxSilentTime = 0;
static bool settingsRead = false;
uid_t callingUID = 0;
bool debugWorker = true;

static std::map<string, Strings> settings;


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

uid_t queryCallingUID()
{
	/* A root user will not even bother calling the daemon, so there is no way to check
	 * If the uid is not yet set...
	 */	 
	return callingUID;	
}

void setCallingUID(uid_t uid, bool reset)
{
	if(callingUID != 0 && !reset)
		throw Error(format("The UID of the caller aleady set! at this point"));
	
	callingUID = uid;
}

string queryCallingUsername()
{
	uid_t uid = queryCallingUID();
	
	passwd *pwd = getpwuid(uid);
	char *pw_name = pwd->pw_name;
	
	return (string)pw_name;
}


     
}
