#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "parser.hh"
#include "eval.hh"
#include "help.txt.hh"


struct Globals
{
    Path linkPath;
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
    Expr e = parseExprFromFile(absPath(nePath));
    if (!parseDerivations(state, e, drvs))
        throw badTerm("expected set of derivations", e);
}


static Path getLinksDir()
{
    return canonPath(nixStateDir + "/links");
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


Path createGeneration(Path outPath, Path drvPath)
{
    Path linksDir = getLinksDir();

    unsigned int num = 0;
    
    Strings names = readDirectory(linksDir);
    for (Strings::iterator i = names.begin(); i != names.end(); ++i) {
        istringstream s(*i);
        unsigned int n; 
        if (s >> n && s.eof() && n >= num) num = n + 1;
    }

    Path generation;

    while (1) {
        generation = (format("%1%/%2%") % linksDir % num).str();
        if (symlink(outPath.c_str(), generation.c_str()) == 0) break;
        if (errno != EEXIST)
            throw SysError(format("creating symlink `%1%'") % generation);
        /* Somebody beat us to it, retry with a higher number. */
        num++;
    }

    writeStringToFile(generation + "-src.id", drvPath);

    return generation;
}


void switchLink(Path link, Path target)
{
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
    const Path & linkPath)
{
    /* Get the environment builder expression. */
    Expr envBuilder = parseExprFromFile(nixDataDir + "/nix/corepkgs/buildenv"); /* !!! */

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
    Path generation = createGeneration(
        topLevelDrv.outPath, topLevelDrv.drvPath);
    switchLink(linkPath, generation);
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
    Path nePath, DrvNames & selectors, const Path & linkPath)
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
    queryInstalled(state, installedDrvs, linkPath);
    selectedDrvs.insert(installedDrvs.begin(), installedDrvs.end());

    createUserEnv(state, selectedDrvs, linkPath);
}


static void opInstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());
    if (opArgs.size() < 1) throw UsageError("Nix file expected");

    Path nePath = opArgs.front();
    DrvNames drvNames = drvNamesFromArgs(
        Strings(++opArgs.begin(), opArgs.end()));
    
    installDerivations(globals.state, nePath, drvNames, globals.linkPath);
}


static void upgradeDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & linkPath)
{
    debug(format("upgrading derivations from `%1%'") % nePath);

    /* Upgrade works as follows: we take all currently installed
       derivations, and for any derivation matching any selector, look
       for a derivation in the input Nix expression that has the same
       name and a higher version number. */

    /* Load the currently installed derivations. */
    DrvInfos installedDrvs;
    queryInstalled(state, installedDrvs, linkPath);

    /* Fetch all derivations from the input file. */
    DrvInfos availDrvs;
    loadDerivations(state, nePath, availDrvs);

    /* Go through all installed derivations. */
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
        if (!upgrade) continue;

        /* If yes, find the derivation in the input Nix expression
           with the same name and the highest version number. */
        DrvInfos::iterator bestDrv = i;
        DrvName bestName = drvName;
        for (DrvInfos::iterator j = availDrvs.begin();
             j != availDrvs.end(); ++j)
        {
            DrvName newName(j->second.name);
            if (newName.name == bestName.name &&
                compareVersions(newName.version, bestName.version) > 0)
                bestDrv = j;
        }

        if (bestDrv != i) {
            printMsg(lvlInfo,
                format("upgrading `%1%' to `%2%'")
                % i->second.name % bestDrv->second.name);
            installedDrvs.erase(i);
            installedDrvs.insert(*bestDrv);
        }
    }
    
    createUserEnv(state, installedDrvs, linkPath);
}


static void opUpgrade(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());
    if (opArgs.size() < 1) throw UsageError("Nix file expected");

    Path nePath = opArgs.front();
    DrvNames drvNames = drvNamesFromArgs(
        Strings(++opArgs.begin(), opArgs.end()));
    
    upgradeDerivations(globals.state, nePath, drvNames, globals.linkPath);
}


static void uninstallDerivations(EvalState & state, DrvNames & selectors,
    Path & linkPath)
{
    DrvInfos installedDrvs;
    queryInstalled(state, installedDrvs, linkPath);

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

    createUserEnv(state, installedDrvs, linkPath);
}


static void opUninstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    DrvNames drvNames = drvNamesFromArgs(opArgs);

    uninstallDerivations(globals.state, drvNames, globals.linkPath);
}


static void opQuery(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    enum { qName, qDrvPath, qStatus } query = qName;
    enum { sInstalled, sAvailable } source = sInstalled;

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--name") query = qName;
        else if (*i == "--expr" || *i == "-e") query = qDrvPath;
        else if (*i == "--status" || *i == "-s") query = qStatus;
        else if (*i == "--installed") source = sInstalled;
        else if (*i == "--available" || *i == "-f") source = sAvailable;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    /* Obtain derivation information from the specified source. */
    DrvInfos drvs;

    switch (source) {

        case sInstalled:
            queryInstalled(globals.state, drvs, globals.linkPath);
            break;

        case sAvailable: {
            if (opArgs.size() < 1) throw UsageError("Nix file expected");
            Path nePath = opArgs.front();
            opArgs.pop_front();
            loadDerivations(globals.state, nePath, drvs);
            break;
        }

        default: abort();
    }

    if (opArgs.size() != 0) throw UsageError("no arguments expected");
    
    /* Perform the specified query on the derivations. */
    switch (query) {

        case qName: {
            for (DrvInfos::iterator i = drvs.begin(); i != drvs.end(); ++i)
                cout << format("%1%\n") % i->second.name;
            break;
        }
        
        case qDrvPath: {
            for (DrvInfos::iterator i = drvs.begin(); i != drvs.end(); ++i)
                cout << format("%1%\n") % i->second.drvPath;
            break;
        }
        
        case qStatus: {
            DrvInfos installed;
            queryInstalled(globals.state, installed, globals.linkPath);

            for (DrvInfos::iterator i = drvs.begin(); i != drvs.end(); ++i) {
                cout << format("%1%%2% %3%\n")
                    % (installed.find(i->first) != installed.end() ? 'I' : '-')
                    % (isValidPath(i->second.outPath) ? 'P' : '-')
                    % i->second.name;
            }
            break;
        }
        
        default: abort();
    }
}


void run(Strings args)
{
    /* Use a higher default verbosity (lvlInfo). */
    verbosity = (Verbosity) ((int) verbosity + 1);

    Strings opFlags, opArgs;
    Operation op = 0;
    
    Globals globals;
    globals.linkPath = getLinksDir() + "/current";

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
        else if (arg == "--link" || arg == "-l") {
            ++i;
            if (i == args.end()) throw UsageError(
                format("`%1%' requires an argument") % arg);
            globals.linkPath = absPath(*i);
        }
        else if (arg[0] == '-')
            opFlags.push_back(arg);
        else
            opArgs.push_back(arg);

        if (oldOp && oldOp != op)
            throw UsageError("only one operation may be specified");
    }

    if (!op) throw UsageError("no operation specified");

    openDB();

    op(globals, opFlags, opArgs);

    printEvalStats(globals.state);
}


string programId = "nix-env";
