#include "profiles.hh"
#include "names.hh"
#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "parser.hh"
#include "eval.hh"
#include "help.txt.hh"

#include <cerrno>
#include <ctime>


struct Globals
{
    Path profile;
    Path nixExprPath;
    EvalState state;
    bool dryRun;
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
        throw Error("set of derivations expected");
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


struct AddPos : TermFun
{
    ATerm operator () (ATerm e)
    {
        ATMatcher m;
        ATerm x, y, z;
        if (atMatch(m, e) >> "Bind" >> x >> y >> z)
            return e;
        if (atMatch(m, e) >> "Bind" >> x >> y)
            return ATmake("Bind(<term>, <term>, NoPos)", x, y);
        return e;
    }
};


void queryInstalled(EvalState & state, DrvInfos & drvs,
    const Path & userEnv)
{
    Path path = userEnv + "/manifest";

    if (!pathExists(path)) return; /* not an error, assume nothing installed */

    Expr e = ATreadFromNamedFile(path.c_str());
    if (!e) throw Error(format("cannot read Nix expression from `%1%'") % path);

    /* Compatibility: Bind(x, y) -> Bind(x, y, NoPos). */
    AddPos addPos;
    e = bottomupRewrite(addPos, e);

    if (!parseDerivations(state, e, drvs))
        throw badTerm(format("set of derivations expected in `%1%'") % path, e);
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
            "Bind(\"type\", Str(\"derivation\"), NoPos), "
            "Bind(\"name\", Str(<str>), NoPos), "
            "Bind(\"drvPath\", Path(<str>), NoPos), "
            "Bind(\"drvHash\", Str(<str>), NoPos), "
            "Bind(\"outPath\", Path(<str>), NoPos)"
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
        "Bind(\"system\", Str(<str>), NoPos), "
        "Bind(\"derivations\", <term>, NoPos), " // !!! redundant
        "Bind(\"manifest\", Path(<str>), NoPos)"
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
        topLevelDrv.outPath, topLevelDrv.drvPath, nfPath);
    switchLink(profile, generation);
}


static void installDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & profile,
    bool dryRun)
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

    if (dryRun) return;

    createUserEnv(state, selectedDrvs, profile);
}


static void opInstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    DrvNames drvNames = drvNamesFromArgs(opArgs);
    
    installDerivations(globals.state, globals.nixExprPath,
        drvNames, globals.profile, globals.dryRun);
}


typedef enum { utLt, utLeq, utAlways } UpgradeType;


static void upgradeDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & profile,
    UpgradeType upgradeType, bool dryRun)
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

        if (!upgrade) {
            newDrvs.insert(*i);
            continue;
        }
            
        /* If yes, find the derivation in the input Nix expression
           with the same name and satisfying the version constraints
           specified by upgradeType.  If there are multiple matches,
           take the one with highest version. */
        DrvInfos::iterator bestDrv = availDrvs.end();
        DrvName bestName;
        for (DrvInfos::iterator j = availDrvs.begin();
             j != availDrvs.end(); ++j)
        {
            DrvName newName(j->second.name);
            if (newName.name == drvName.name) {
                int d = compareVersions(drvName.version, newName.version);
                if (upgradeType == utLt && d < 0 ||
                    upgradeType == utLeq && d <= 0 ||
                    upgradeType == utAlways)
                {
                    if (bestDrv == availDrvs.end() ||
                        compareVersions(
                            bestName.version, newName.version) < 0)
                    {
                        bestDrv = j;
                        bestName = newName;
                    }
                }
            }
        }

        if (bestDrv != availDrvs.end() &&
            i->second.drvPath != bestDrv->second.drvPath)
        {
            printMsg(lvlInfo,
                format("upgrading `%1%' to `%2%'")
                % i->second.name % bestDrv->second.name);
            newDrvs.insert(*bestDrv);
        } else newDrvs.insert(*i);
    }
    
    if (dryRun) return;

    createUserEnv(state, newDrvs, profile);
}


static void opUpgrade(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    UpgradeType upgradeType = utLt;
    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--lt") upgradeType = utLt;
        else if (*i == "--leq") upgradeType = utLeq;
        else if (*i == "--always") upgradeType = utAlways;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    DrvNames drvNames = drvNamesFromArgs(opArgs);
    
    upgradeDerivations(globals.state, globals.nixExprPath,
        drvNames, globals.profile, upgradeType, globals.dryRun);
}


static void uninstallDerivations(EvalState & state, DrvNames & selectors,
    Path & profile, bool dryRun)
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

    if (dryRun) return;

    createUserEnv(state, installedDrvs, profile);
}


static void opUninstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    DrvNames drvNames = drvNamesFromArgs(opArgs);

    uninstallDerivations(globals.state, drvNames,
        globals.profile, globals.dryRun);
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
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("exactly one argument expected"));

    Path profile = opArgs.front();
    Path profileLink = getHomeDir() + "/.nix-profile";

    switchLink(profileLink, profile);
}


static const int prevGen = -2;


static void switchGeneration(Globals & globals, int dstGen)
{
    int curGen;
    Generations gens = findGenerations(globals.profile, curGen);

    Generation dst;
    for (Generations::iterator i = gens.begin(); i != gens.end(); ++i)
        if ((dstGen == prevGen && i->number < curGen) ||
            (dstGen >= 0 && i->number == dstGen))
            dst = *i;

    if (!dst)
        if (dstGen == prevGen)
            throw Error(format("no generation older than the current (%1%) exists")
                % curGen);
        else
            throw Error(format("generation %1% does not exist") % dstGen);

    printMsg(lvlInfo, format("switching from generation %1% to %2%")
        % curGen % dst.number);
    
    if (globals.dryRun) return;
    
    switchLink(globals.profile, dst.path);
}


static void opSwitchGeneration(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("exactly one argument expected"));

    istringstream str(opArgs.front());
    int dstGen;
    str >> dstGen;
    if (!str || !str.eof())
        throw UsageError(format("expected a generation number"));

    switchGeneration(globals, dstGen);
}


static void opRollback(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError(format("no arguments expected"));

    switchGeneration(globals, prevGen);
}


static void opListGenerations(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError(format("no arguments expected"));

    int curGen;
    Generations gens = findGenerations(globals.profile, curGen);

    for (Generations::iterator i = gens.begin(); i != gens.end(); ++i) {
        tm t;
        if (!localtime_r(&i->creationTime, &t)) throw Error("cannot convert time");
        cout << format("%|4|   %|4|-%|02|-%|02| %|02|:%|02|:%|02|   %||\n")
            % i->number
            % (t.tm_year + 1900) % (t.tm_mon + 1) % t.tm_mday
            % t.tm_hour % t.tm_min % t.tm_sec
            % (i->number == curGen ? "(current)" : "");
    }
}


static void opDefaultExpr(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("exactly one argument expected"));

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
    globals.dryRun = false;

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
        else if (arg == "--switch-generation" || arg == "-G")
            op = opSwitchGeneration;
        else if (arg == "--rollback")
            op = opRollback;
        else if (arg == "--list-generations")
            op = opListGenerations;
        else if (arg == "--dry-run") {
            printMsg(lvlInfo, "(dry run; not doing anything)");
            globals.dryRun = true;
        }
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
