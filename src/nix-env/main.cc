#include <cerrno>

#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "parser.hh"
#include "eval.hh"
#include "help.txt.hh"


struct Globals
{
    Path profile;
    Path nixExprPath;
    EvalState state;
};


typedef void (* Operation) (Globals & globals,
    Strings opFlags, Strings opArgs);


struct DrvInfo
{
    string name;
    Path drvPath;
    Path outPath;
    Hash drvHash;
};

typedef map<Path, DrvInfo> DrvInfos;
typedef vector<DrvInfo> DrvInfoList;


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


bool parseDerivation(EvalState & state, Expr e, DrvInfo & drv)
{
    ATMatcher m;
    
    e = evalExpr(state, e);
    if (!(atMatch(m, e) >> "Attrs")) return false;
    Expr a = queryAttr(e, "type");
    if (!a || evalString(state, a) != "derivation") return false;

    a = queryAttr(e, "name");
    if (!a) throw badTerm("derivation name missing", e);
    drv.name = evalString(state, a);

    a = queryAttr(e, "drvPath");
    if (!a) throw badTerm("derivation path missing", e);
    drv.drvPath = evalPath(state, a);

    a = queryAttr(e, "drvHash");
    if (!a) throw badTerm("derivation hash missing", e);
    drv.drvHash = parseHash(evalString(state, a));

    a = queryAttr(e, "outPath");
    if (!a) throw badTerm("output path missing", e);
    drv.outPath = evalPath(state, a);

    return true;
}


bool parseDerivations(EvalState & state, Expr e, DrvInfos & drvs)
{
    ATMatcher m;
    ATermList es;
    DrvInfo drv;

    e = evalExpr(state, e);

    if (parseDerivation(state, e, drv)) 
        drvs[drv.drvPath] = drv;

    else if (atMatch(m, e) >> "Attrs") {
        ATermMap drvMap;
        queryAllAttrs(e, drvMap);
        for (ATermIterator i(drvMap.keys()); i; ++i) {
            debug(format("evaluating attribute `%1%'") % *i);
            if (parseDerivation(state, drvMap.get(*i), drv))
                drvs[drv.drvPath] = drv;
        }
    }

    else if (atMatch(m, e) >> "List" >> es) {
        for (ATermIterator i(es); i; ++i) {
            debug(format("evaluating list element"));
            if (parseDerivation(state, *i, drv))
                drvs[drv.drvPath] = drv;
        }
    }

    return true;
}


void loadDerivations(EvalState & state, Path nePath, DrvInfos & drvs)
{
    Expr e = parseExprFromFile(state, absPath(nePath));
    if (!parseDerivations(state, e, drvs))
        throw badTerm("expected set of derivations", e);
}


static Path getHomeDir()
{
    Path homeDir(getenv("HOME"));
    if (homeDir == "") throw Error("HOME environment variable not set");
    return homeDir;
}


static Path getDefNixExprPath()
{
    return getHomeDir() + "/.nix-defexpr";
}


void queryInstalled(EvalState & state, DrvInfos & drvs,
    const Path & userEnv)
{
    Path path = userEnv + "/manifest";

    if (!pathExists(path)) return; /* not an error, assume nothing installed */

    Expr e = ATreadFromNamedFile(path.c_str());
    if (!e) throw Error(format("cannot read Nix expression from `%1%'") % path);

    if (!parseDerivations(state, e, drvs))
        throw badTerm(format("expected set of derivations in `%1%'") % path, e);
}


Path createGeneration(Path profile, Path outPath, Path drvPath)
{
    Path profileDir = dirOf(profile);
    string profileName = baseNameOf(profile);
    
    unsigned int num = 0;
    
    Strings names = readDirectory(profileDir);
    for (Strings::iterator i = names.begin(); i != names.end(); ++i) {
        if (string(*i, 0, profileName.size() + 1) != profileName + "-") continue;
        string s = string(*i, profileName.size() + 1);
        int p = s.find("-link");
        if (p == string::npos) continue;
        istringstream str(string(s, 0, p));
        unsigned int n;
        if (str >> n && str.eof() && n >= num) num = n + 1;
    }

    Path generation, gcrootSrc;

    while (1) {
        Path prefix = (format("%1%-%2%") % profile % num).str();
        generation = prefix + "-link";
        gcrootSrc = prefix + "-src.gcroot";
        if (symlink(outPath.c_str(), generation.c_str()) == 0) break;
        if (errno != EEXIST)
            throw SysError(format("creating symlink `%1%'") % generation);
        /* Somebody beat us to it, retry with a higher number. */
        num++;
    }

    writeStringToFile(gcrootSrc, drvPath);

    return generation;
}


void switchLink(Path link, Path target)
{
    /* Hacky. */
    if (dirOf(target) == dirOf(link)) target = baseNameOf(target);
    
    Path tmp = canonPath(dirOf(link) + "/.new_" + baseNameOf(link));
    if (symlink(target.c_str(), tmp.c_str()) != 0)
        throw SysError(format("creating symlink `%1%'") % tmp);
    /* The rename() system call is supposed to be essentially atomic
       on Unix.  That is, if we have links `current -> X' and
       `new_current -> Y', and we rename new_current to current, a
       process accessing current will see X or Y, but never a
       file-not-found or other error condition.  This is sufficient to
       atomically switch user environments. */
    if (rename(tmp.c_str(), link.c_str()) != 0)
        throw SysError(format("renaming `%1%' to `%2%'") % tmp % link);
}


void createUserEnv(EvalState & state, const DrvInfos & drvs,
    const Path & profile)
{
    /* Get the environment builder expression. */
    Expr envBuilder = parseExprFromFile(state,
        nixDataDir + "/nix/corepkgs/buildenv"); /* !!! */

    /* Construct the whole top level derivation. */
    ATermList inputs = ATempty;
    for (DrvInfos::const_iterator i = drvs.begin(); 
         i != drvs.end(); ++i)
    {
        ATerm t = ATmake(
            "Attrs(["
            "Bind(\"type\", Str(\"derivation\")), "
            "Bind(\"name\", Str(<str>)), "
            "Bind(\"drvPath\", Path(<str>)), "
            "Bind(\"drvHash\", Str(<str>)), "
            "Bind(\"outPath\", Path(<str>))"
            "])",
            i->second.name.c_str(),
            i->second.drvPath.c_str(),
            ((string) i->second.drvHash).c_str(),
            i->second.outPath.c_str());
        inputs = ATinsert(inputs, t);
    }

    ATerm inputs2 = ATmake("List(<term>)", ATreverse(inputs));

    /* Also write a copy of the list of inputs to the store; we need
       it for future modifications of the environment. */
    Path inputsFile = writeTerm(inputs2, "-env-inputs");

    Expr topLevel = ATmake(
        "Call(<term>, Attrs(["
        "Bind(\"system\", Str(<str>)), "
        "Bind(\"derivations\", <term>), " // !!! redundant
        "Bind(\"manifest\", Path(<str>))"
        "]))",
        envBuilder, thisSystem.c_str(), inputs2, inputsFile.c_str());

    /* Instantiate it. */
    debug(format("evaluating builder expression `%1%'") % topLevel);
    DrvInfo topLevelDrv;
    if (!parseDerivation(state, topLevel, topLevelDrv))
        abort();
    
    /* Realise the resulting store expression. */
    debug(format("realising user environment"));
    Path nfPath = normaliseStoreExpr(topLevelDrv.drvPath);
    realiseClosure(nfPath);

    /* Switch the current user environment to the output path. */
    debug(format("switching to new user environment"));
    Path generation = createGeneration(profile,
        topLevelDrv.outPath, topLevelDrv.drvPath);
    switchLink(profile, generation);
}


struct DrvName
{
    string fullName;
    string name;
    string version;
    unsigned int hits;

    /* Parse a derivation name.  The `name' part of a derivation name
       is everything up to but not including the first dash *not*
       followed by a letter.  The `version' part is the rest
       (excluding the separating dash).  E.g., `apache-httpd-2.0.48'
       is parsed to (`apache-httpd', '2.0.48'). */
    DrvName(const string & s) : hits(0)
    {
        name = fullName = s;
        for (unsigned int i = 0; i < s.size(); ++i) {
            /* !!! isalpha/isdigit are affected by the locale. */
            if (s[i] == '-' && i + 1 < s.size() && !isalpha(s[i + 1])) {
                name = string(s, 0, i);
                version = string(s, i + 1);
                break;
            }
        }
    }

    bool matches(DrvName & n)
    {
        if (name != "*" && name != n.name) return false;
        if (version != "" && version != n.version) return false;
        return true;
    }
};


string nextComponent(string::const_iterator & p,
    const string::const_iterator end)
{
    /* Skip any dots and dashes (component separators). */
    while (p != end && (*p == '.' || *p == '-')) ++p;

    if (p == end) return "";

    /* If the first character is a digit, consume the longest sequence
       of digits.  Otherwise, consume the longest sequence of
       non-digit, non-separator characters. */
    string s;
    if (isdigit(*p))
        while (p != end && isdigit(*p)) s += *p++;
    else
        while (p != end && (!isdigit(*p) && *p != '.' && *p != '-'))
            s += *p++;
    
    return s;
}


#include <fstream>

bool parseInt(const string & s, int & n)
{
    istringstream st(s);
    st >> n;
    return !st.fail();
}


static bool componentsLT(const string & c1, const string & c2)
{
    int n1, n2;
    bool c1Num = parseInt(c1, n1), c2Num = parseInt(c2, n2);

    if (c1Num && c2Num) return n1 < n2;
    else if (c1 == "" && c2Num) return true;
    else if (c1 == "pre" && c2 != "pre") return true;
    else if (c2 == "pre") return false;
    /* Assume that `2.3a' < `2.3.1'. */
    else if (c2Num) return true;
    else if (c1Num) return false;
    else return c1 < c2;
}


static int compareVersions(const string & v1, const string & v2)
{
    string::const_iterator p1 = v1.begin();
    string::const_iterator p2 = v2.begin();
    
    while (p1 != v1.end() || p2 != v2.end()) {
        string c1 = nextComponent(p1, v1.end());
        string c2 = nextComponent(p2, v2.end());
        if (componentsLT(c1, c2)) return -1;
        else if (componentsLT(c2, c1)) return 1;
    }

    return 0;
}


static void testCompareVersions()
{
#define TEST(v1, v2, n) assert( \
    compareVersions(v1, v2) == n && compareVersions(v2, v1) == -n)
    TEST("1.0", "2.3", -1);
    TEST("2.1", "2.3", -1);
    TEST("2.3", "2.3", 0);
    TEST("2.5", "2.3", 1);
    TEST("3.1", "2.3", 1);
    TEST("2.3.1", "2.3", 1);
    TEST("2.3.1", "2.3a", 1);
    TEST("2.3pre1", "2.3", -1);
    TEST("2.3pre3", "2.3pre12", -1);
    TEST("2.3a", "2.3c", -1);
    TEST("2.3pre1", "2.3c", -1);
    TEST("2.3pre1", "2.3q", -1);
}


typedef list<DrvName> DrvNames;


static DrvNames drvNamesFromArgs(const Strings & opArgs)
{
    DrvNames result;
    for (Strings::const_iterator i = opArgs.begin();
         i != opArgs.end(); ++i)
        result.push_back(DrvName(*i));
    return result;
}


static void installDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & profile)
{
    debug(format("installing derivations from `%1%'") % nePath);

    /* Fetch all derivations from the input file. */
    DrvInfos availDrvs;
    loadDerivations(state, nePath, availDrvs);

    /* Filter out the ones we're not interested in. */
    DrvInfos selectedDrvs;
    for (DrvInfos::iterator i = availDrvs.begin();
         i != availDrvs.end(); ++i)
    {
        DrvName drvName(i->second.name);
        for (DrvNames::iterator j = selectors.begin();
             j != selectors.end(); ++j)
        {
            if (j->matches(drvName)) {
                printMsg(lvlInfo,
                    format("installing `%1%'") % i->second.name);
                j->hits++;
                selectedDrvs.insert(*i);
            }
        }
    }

    /* Check that all selectors have been used. */
    for (DrvNames::iterator i = selectors.begin();
         i != selectors.end(); ++i)
        if (i->hits == 0)
            throw Error(format("selector `%1%' matches no derivations")
                % i->fullName);
    
    /* Add in the already installed derivations. */
    DrvInfos installedDrvs;
    queryInstalled(state, installedDrvs, profile);
    selectedDrvs.insert(installedDrvs.begin(), installedDrvs.end());

    createUserEnv(state, selectedDrvs, profile);
}


static void opInstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    DrvNames drvNames = drvNamesFromArgs(opArgs);
    
    installDerivations(globals.state, globals.nixExprPath,
        drvNames, globals.profile);
}


static void upgradeDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & profile)
{
    debug(format("upgrading derivations from `%1%'") % nePath);

    /* Upgrade works as follows: we take all currently installed
       derivations, and for any derivation matching any selector, look
       for a derivation in the input Nix expression that has the same
       name and a higher version number. */

    /* Load the currently installed derivations. */
    DrvInfos installedDrvs;
    queryInstalled(state, installedDrvs, profile);

    /* Fetch all derivations from the input file. */
    DrvInfos availDrvs;
    loadDerivations(state, nePath, availDrvs);

    /* Go through all installed derivations. */
    DrvInfos newDrvs;
    for (DrvInfos::iterator i = installedDrvs.begin();
         i != installedDrvs.end(); ++i)
    {
        DrvName drvName(i->second.name);

        /* Do we want to upgrade this derivation? */
        bool upgrade = false;
        for (DrvNames::iterator j = selectors.begin();
             j != selectors.end(); ++j)
        {
            if (j->matches(drvName)) {
                j->hits++;
                upgrade = true;
                break;
            }
        }

        /* If yes, find the derivation in the input Nix expression
           with the same name and the highest version number. */
        DrvInfos::iterator bestDrv = i;
        DrvName bestName = drvName;
        if (upgrade) {
            for (DrvInfos::iterator j = availDrvs.begin();
                 j != availDrvs.end(); ++j)
            {
                DrvName newName(j->second.name);
                if (newName.name == bestName.name &&
                    compareVersions(newName.version, bestName.version) > 0)
                    bestDrv = j;
            }
        }

        if (bestDrv != i) {
            printMsg(lvlInfo,
                format("upgrading `%1%' to `%2%'")
                % i->second.name % bestDrv->second.name);
        }
        
        newDrvs.insert(*bestDrv);
    }
    
    createUserEnv(state, newDrvs, profile);
}


static void opUpgrade(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());
    if (opArgs.size() < 1) throw UsageError("Nix file expected");

    DrvNames drvNames = drvNamesFromArgs(opArgs);
    
    upgradeDerivations(globals.state, globals.nixExprPath,
        drvNames, globals.profile);
}


static void uninstallDerivations(EvalState & state, DrvNames & selectors,
    Path & profile)
{
    DrvInfos installedDrvs;
    queryInstalled(state, installedDrvs, profile);

    for (DrvInfos::iterator i = installedDrvs.begin();
         i != installedDrvs.end(); ++i)
    {
        DrvName drvName(i->second.name);
        for (DrvNames::iterator j = selectors.begin();
             j != selectors.end(); ++j)
            if (j->matches(drvName)) {
                printMsg(lvlInfo,
                    format("uninstalling `%1%'") % i->second.name);
                installedDrvs.erase(i);
            }
    }

    createUserEnv(state, installedDrvs, profile);
}


static void opUninstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    DrvNames drvNames = drvNamesFromArgs(opArgs);

    uninstallDerivations(globals.state, drvNames, globals.profile);
}


static bool cmpDrvByName(const DrvInfo & a, const DrvInfo & b)
{
    return a.name < b.name;
}


static void opQuery(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    enum { qName, qDrvPath, qStatus } query = qName;
    enum { sInstalled, sAvailable } source = sInstalled;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--name") query = qName;
        else if (*i == "--expr") query = qDrvPath;
        else if (*i == "--status" || *i == "-s") query = qStatus;
        else if (*i == "--installed") source = sInstalled;
        else if (*i == "--available" || *i == "-a") source = sAvailable;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    /* Obtain derivation information from the specified source. */
    DrvInfos drvs;

    switch (source) {

        case sInstalled:
            queryInstalled(globals.state, drvs, globals.profile);
            break;

        case sAvailable: {
            loadDerivations(globals.state, globals.nixExprPath, drvs);
            break;
        }

        default: abort();
    }

    if (opArgs.size() != 0) throw UsageError("no arguments expected");

    /* Sort them by name. */
    DrvInfoList drvs2;
    for (DrvInfos::iterator i = drvs.begin(); i != drvs.end(); ++i)
        drvs2.push_back(i->second);
    sort(drvs2.begin(), drvs2.end(), cmpDrvByName);
    
    /* Perform the specified query on the derivations. */
    switch (query) {

        case qName: {
            for (DrvInfoList::iterator i = drvs2.begin(); i != drvs2.end(); ++i)
                cout << format("%1%\n") % i->name;
            break;
        }
        
        case qDrvPath: {
            for (DrvInfoList::iterator i = drvs2.begin(); i != drvs2.end(); ++i)
                cout << format("%1%\n") % i->drvPath;
            break;
        }
        
        case qStatus: {
            DrvInfos installed;
            queryInstalled(globals.state, installed, globals.profile);

            PathSet installedPaths; /* output paths of installed drvs */
            for (DrvInfos::iterator i = installed.begin();
                 i != installed.end(); ++i)
                installedPaths.insert(i->second.outPath);
            
            for (DrvInfoList::iterator i = drvs2.begin(); i != drvs2.end(); ++i) {
                Paths subs = querySubstitutes(i->drvPath);
                cout << format("%1%%2%%3% %4%\n")
                    % (installedPaths.find(i->outPath)
                        != installedPaths.end() ? 'I' : '-')
                    % (isValidPath(i->outPath) ? 'P' : '-')
                    % (subs.size() > 0 ? 'S' : '-')
                    % i->name;
            }
            break;
        }
        
        default: abort();
    }
}


static void opSwitchProfile(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("`--profile' takes exactly one argument"));

    Path profile = opArgs.front();
    Path profileLink = getHomeDir() + "/.nix-profile";

    switchLink(profileLink, profile);
}


static void opDefaultExpr(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("`--import' takes exactly one argument"));

    Path defNixExpr = absPath(opArgs.front());
    Path defNixExprLink = getDefNixExprPath();
    
    switchLink(defNixExprLink, defNixExpr);
}


void run(Strings args)
{
    /* Use a higher default verbosity (lvlInfo). */
    verbosity = (Verbosity) ((int) verbosity + 1);

    Strings opFlags, opArgs;
    Operation op = 0;
    
    Globals globals;
    globals.nixExprPath = getDefNixExprPath();

    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;

        Operation oldOp = op;

        if (arg == "--install" || arg == "-i")
            op = opInstall;
        else if (arg == "--uninstall" || arg == "-e")
            op = opUninstall;
        else if (arg == "--upgrade" || arg == "-u")
            op = opUpgrade;
        else if (arg == "--query" || arg == "-q")
            op = opQuery;
        else if (arg == "--import" || arg == "-I") /* !!! bad name */
            op = opDefaultExpr;
        else if (arg == "--profile" || arg == "-p") {
            ++i;
            if (i == args.end()) throw UsageError(
                format("`%1%' requires an argument") % arg);
            globals.profile = absPath(*i);
        }
        else if (arg == "--file" || arg == "-f") {
            ++i;
            if (i == args.end()) throw UsageError(
                format("`%1%' requires an argument") % arg);
            globals.nixExprPath = absPath(*i);
        }
        else if (arg == "--switch-profile" || arg == "-S")
            op = opSwitchProfile;
        else if (arg[0] == '-')
            opFlags.push_back(arg);
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }

    if (!op) throw UsageError("no operation specified");

    if (globals.profile == "") {
        Path profileLink = getHomeDir() + "/.nix-profile";
        globals.profile = pathExists(profileLink)
            ? absPath(readLink(profileLink), dirOf(profileLink))
            : canonPath(nixStateDir + "/profiles/default");
    }
    
    openDB();

    op(globals, opFlags, opArgs);

    printEvalStats(globals.state);
}


string programId = "nix-env";
