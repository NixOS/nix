#include "profiles.hh"
#include "names.hh"
#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "parser.hh"
#include "eval.hh"
#include "help.txt.hh"
#include "nixexpr-ast.hh"

#include <cerrno>
#include <ctime>


struct Globals
{
    Path profile;
    Path nixExprPath;
    EvalState state;
    bool dryRun;
    bool preserveInstalled;
    string systemFilter;
};


typedef void (* Operation) (Globals & globals,
    Strings opFlags, Strings opArgs);


struct DrvInfo
{
    string name;
    string system;
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
    ATermList es;
    e = evalExpr(state, e);
    if (!matchAttrs(e, es)) return false;
    Expr a = queryAttr(e, "type");
    if (!a || evalString(state, a) != "derivation") return false;

    a = queryAttr(e, "name");
    if (!a) throw badTerm("derivation name missing", e);
    drv.name = evalString(state, a);

    a = queryAttr(e, "system");
    if (!a)
        drv.system = "unknown";
    else
        drv.system = evalString(state, a);

    a = queryAttr(e, "drvPath");
    if (!a) throw badTerm("derivation path missing", e);
    drv.drvPath = evalPath(state, a);

    a = queryAttr(e, "drvHash");
    if (!a) throw badTerm("derivation hash missing", e);
    drv.drvHash = parseHash(htMD5, evalString(state, a));

    a = queryAttr(e, "outPath");
    if (!a) throw badTerm("output path missing", e);
    drv.outPath = evalPath(state, a);

    return true;
}


bool parseDerivations(EvalState & state, Expr e, DrvInfos & drvs)
{
    ATermList es;
    DrvInfo drv;

    e = evalExpr(state, e);

    if (parseDerivation(state, e, drv)) 
        drvs[drv.drvPath] = drv;

    else if (matchAttrs(e, es)) {
        ATermMap drvMap;
        queryAllAttrs(e, drvMap);
        for (ATermIterator i(drvMap.keys()); i; ++i) {
            debug(format("evaluating attribute `%1%'") % *i);
            if (parseDerivation(state, drvMap.get(*i), drv))
                drvs[drv.drvPath] = drv;
            else
                parseDerivations(state, drvMap.get(*i), drvs);
        }
    }

    else if (matchList(e, es)) {
        for (ATermIterator i(es); i; ++i) {
            debug(format("evaluating list element"));
            if (parseDerivation(state, *i, drv))
                drvs[drv.drvPath] = drv;
            else
                parseDerivations(state, *i, drvs);
        }
    }

    return true;
}


void loadDerivations(EvalState & state, Path nePath, DrvInfos & drvs,
    string systemFilter)
{
    Expr e = parseExprFromFile(state, absPath(nePath));
    if (!parseDerivations(state, e, drvs))
        throw Error("set of derivations expected");

    /* Filter out all derivations not applicable to the current
       system. */
    for (DrvInfos::iterator i = drvs.begin(), j; i != drvs.end(); i = j) {
        j = i; j++;
        if (systemFilter != "*" && i->second.system != systemFilter)
            drvs.erase(i);
    }
}


static Path getHomeDir()
{
    Path homeDir(getEnv("HOME", ""));
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
        ATerm x, y, z;
        if (matchBind(e, x, y, z)) return e;
        if (matchBind2(e, x, y))
            return makeBind(x, y, makeNoPos());
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
        ATerm t = makeAttrs(ATmakeList6(
            makeBind(toATerm("type"),
                makeStr(toATerm("derivation")), makeNoPos()),
            makeBind(toATerm("name"),
                makeStr(toATerm(i->second.name)), makeNoPos()),
            makeBind(toATerm("system"),
                makeStr(toATerm(i->second.system)), makeNoPos()),
            makeBind(toATerm("drvPath"),
                makePath(toATerm(i->second.drvPath)), makeNoPos()),
            makeBind(toATerm("drvHash"),
                makeStr(toATerm(printHash(i->second.drvHash))), makeNoPos()),
            makeBind(toATerm("outPath"),
                makePath(toATerm(i->second.outPath)), makeNoPos())
            ));
        inputs = ATinsert(inputs, t);
    }

    ATerm inputs2 = makeList(ATreverse(inputs));

    /* Also write a copy of the list of inputs to the store; we need
       it for future modifications of the environment. */
    Path inputsFile = writeTerm(inputs2, "env-inputs");

    Expr topLevel = makeCall(envBuilder, makeAttrs(ATmakeList3(
        makeBind(toATerm("system"),
            makeStr(toATerm(thisSystem)), makeNoPos()),
        makeBind(toATerm("derivations"), inputs2, makeNoPos()),
        makeBind(toATerm("manifest"),
            makePath(toATerm(inputsFile)), makeNoPos())
        )));

    /* Instantiate it. */
    debug(format("evaluating builder expression `%1%'") % topLevel);
    DrvInfo topLevelDrv;
    if (!parseDerivation(state, topLevel, topLevelDrv))
        abort();
    
    /* Realise the resulting store expression. */
    debug(format("realising user environment"));
    Path nfPath = realiseStoreExpr(topLevelDrv.drvPath);

    /* Switch the current user environment to the output path. */
    debug(format("switching to new user environment"));
    Path generation = createGeneration(profile,
        topLevelDrv.outPath, topLevelDrv.drvPath, nfPath);
    switchLink(profile, generation);
}


static void installDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & profile,
    bool dryRun, bool preserveInstalled, string systemFilter)
{
    debug(format("installing derivations from `%1%'") % nePath);

    /* Fetch all derivations from the input file. */
    DrvInfos availDrvs;
    loadDerivations(state, nePath, availDrvs, systemFilter);

    /* Filter out the ones we're not interested in. */
    DrvInfos selectedDrvs;
    StringSet selectedNames;
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
                selectedNames.insert(drvName.name);
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

    for (DrvInfos::iterator i = installedDrvs.begin();
         i != installedDrvs.end(); ++i)
    {
        DrvName drvName(i->second.name);
        if (!preserveInstalled &&
            selectedNames.find(drvName.name) != selectedNames.end())
            printMsg(lvlInfo,
                format("uninstalling `%1%'") % i->second.name);
        else
            selectedDrvs.insert(*i);
    }

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
        drvNames, globals.profile, globals.dryRun,
        globals.preserveInstalled, globals.systemFilter);
}


typedef enum { utLt, utLeq, utAlways } UpgradeType;


static void upgradeDerivations(EvalState & state,
    Path nePath, DrvNames & selectors, const Path & profile,
    UpgradeType upgradeType, bool dryRun, string systemFilter)
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
    loadDerivations(state, nePath, availDrvs, systemFilter);

    /* Go through all installed derivations. */
    DrvInfos newDrvs;
    for (DrvInfos::iterator i = installedDrvs.begin();
         i != installedDrvs.end(); ++i)
    {
        DrvName drvName(i->second.name);
        DrvName selector;

        /* Do we want to upgrade this derivation? */
        bool upgrade = false;
        for (DrvNames::iterator j = selectors.begin();
             j != selectors.end(); ++j)
        {
            if (j->name == "*" || j->name == drvName.name) {
                j->hits++;
                selector = *j;
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
                    if (selector.matches(newName) &&
                        (bestDrv == availDrvs.end() ||
                         compareVersions(
                             bestName.version, newName.version) < 0))
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
        drvNames, globals.profile, upgradeType, globals.dryRun,
        globals.systemFilter);
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


static bool cmpChars(char a, char b)
{
    return toupper(a) < toupper(b);
}


static bool cmpDrvByName(const DrvInfo & a, const DrvInfo & b)
{
    return lexicographical_compare(
        a.name.begin(), a.name.end(),
        b.name.begin(), b.name.end(), cmpChars);
}


typedef list<Strings> Table;


void printTable(Table & table)
{
    unsigned int nrColumns = table.size() > 0 ? table.front().size() : 0;
    
    vector<unsigned int> widths;
    widths.resize(nrColumns);
    
    for (Table::iterator i = table.begin(); i != table.end(); ++i) {
        assert(i->size() == nrColumns);
        Strings::iterator j;
        unsigned int column;
        for (j = i->begin(), column = 0; j != i->end(); ++j, ++column)
            if (j->size() > widths[column]) widths[column] = j->size();
    }

    for (Table::iterator i = table.begin(); i != table.end(); ++i) { 
        Strings::iterator j;
        unsigned int column;
        for (j = i->begin(), column = 0; j != i->end(); ++j, ++column)
        {
            cout << *j;
            if (column < nrColumns - 1)
                cout << string(widths[column] - j->size() + 2, ' ');
        }
        cout << endl;
    }
}


static void opQuery(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    bool printStatus = false;
    bool printName = true;
    bool printSystem = false;
    bool printDrvPath = false;

    enum { sInstalled, sAvailable } source = sInstalled;

    readOnlyMode = true; /* makes evaluation a bit faster */

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--status" || *i == "-s") printStatus = true;
        else if (*i == "--no-name") printName = false;
        else if (*i == "--system") printSystem = true;
        else if (*i == "--expr") printDrvPath = true;
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
            loadDerivations(globals.state, globals.nixExprPath,
                drvs, globals.systemFilter);
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

    /* We only need to know the installed paths when we are querying
       the status of the derivation. */
    PathSet installedPaths; /* output paths of installed drvs */
    
    if (printStatus) {
        DrvInfos installed;
        queryInstalled(globals.state, installed, globals.profile);
        
        for (DrvInfos::iterator i = installed.begin();
             i != installed.end(); ++i)
            installedPaths.insert(i->second.outPath);
    }
            
    /* Print the desired columns. */
    Table table;
    
    for (DrvInfoList::iterator i = drvs2.begin(); i != drvs2.end(); ++i) {

        Strings columns;
        
        if (printStatus) {
            Substitutes subs = querySubstitutes(i->drvPath);
            columns.push_back(
                (string) (installedPaths.find(i->outPath)
                    != installedPaths.end() ? "I" : "-")
                + (isValidPath(i->outPath) ? "P" : "-")
                + (subs.size() > 0 ? "S" : "-"));
        }

        if (printName) columns.push_back(i->name);

        if (printSystem) columns.push_back(i->system);

        if (printDrvPath) columns.push_back(i->drvPath);

        table.push_back(columns);
    }

    printTable(table);
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

    SwitchToOriginalUser sw;
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

    int dstGen;
    if (!string2Int(opArgs.front(), dstGen))
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


static void deleteGeneration2(const Path & profile, unsigned int gen)
{
    printMsg(lvlInfo, format("removing generation %1%") % gen);
    deleteGeneration(profile, gen);
}


static void opDeleteGenerations(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());

    int curGen;
    Generations gens = findGenerations(globals.profile, curGen);

    for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i) {

        if (*i == "old") {
            for (Generations::iterator j = gens.begin(); j != gens.end(); ++j)
                if (j->number != curGen)
                    deleteGeneration2(globals.profile, j->number);
        }

        else {
            int n;
            if (!string2Int(*i, n) || n < 0)
                throw UsageError(format("invalid generation specifier `%1%'")  % *i);
            bool found = false;
            for (Generations::iterator j = gens.begin(); j != gens.end(); ++j) {
                if (j->number == n) {
                    deleteGeneration2(globals.profile, j->number);
                    found = true;
                    break;
                }
            }
            if (!found)
                printMsg(lvlError, format("generation %1% does not exist") % n);
        }
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
    
    SwitchToOriginalUser sw;
    switchLink(defNixExprLink, defNixExpr);
}


void run(Strings args)
{
    Strings opFlags, opArgs;
    Operation op = 0;
    
    Globals globals;
    globals.nixExprPath = getDefNixExprPath();
    globals.dryRun = false;
    globals.preserveInstalled = false;
    globals.systemFilter = thisSystem;

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
        else if (arg == "--delete-generations")
            op = opDeleteGenerations;
        else if (arg == "--dry-run") {
            printMsg(lvlInfo, "(dry run; not doing anything)");
            globals.dryRun = true;
        }
        else if (arg == "--preserve-installed" || arg == "-P")
            globals.preserveInstalled = true;
        else if (arg == "--system-filter") {
            ++i;
            if (i == args.end()) throw UsageError(
                format("`%1%' requires an argument") % arg);
            globals.systemFilter = *i;
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
        SwitchToOriginalUser sw;
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
