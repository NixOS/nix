#include "profiles.hh"
#include "names.hh"
#include "globals.hh"
#include "build.hh"
#include "gc.hh"
#include "shared.hh"
#include "parser.hh"
#include "eval.hh"
#include "help.txt.hh"
#include "nixexpr-ast.hh"

#include <cerrno>
#include <ctime>


typedef enum {
    srcNixExprDrvs,
    srcNixExprs,
    srcStorePaths,
    srcProfile,
    srcUnknown
} InstallSourceType;


struct InstallSourceInfo
{
    InstallSourceType type;
    Path nixExprPath; /* for srcNixExprDrvs, srcNixExprs */
    Path profile; /* for srcProfile */
    string systemFilter; /* for srcNixExprDrvs */
};


struct Globals
{
    InstallSourceInfo instSource;
    Path profile;
    EvalState state;
    bool dryRun;
    bool preserveInstalled;
    bool keepDerivations;
};


typedef void (* Operation) (Globals & globals,
    Strings opFlags, Strings opArgs);


struct UserEnvElem
{
    string name;
    string system;
    Path drvPath;
    Path outPath;
};

typedef map<Path, UserEnvElem> UserEnvElems;


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


static bool parseDerivation(EvalState & state, Expr e, UserEnvElem & elem)
{
    ATermList es;
    e = evalExpr(state, e);
    if (!matchAttrs(e, es)) return false;
    Expr a = queryAttr(e, "type");
    if (!a || evalString(state, a) != "derivation") return false;

    a = queryAttr(e, "name");
    if (!a) throw badTerm("derivation name missing", e);
    elem.name = evalString(state, a);

    a = queryAttr(e, "system");
    if (!a)
        elem.system = "unknown";
    else
        elem.system = evalString(state, a);

    a = queryAttr(e, "drvPath");
    if (a) elem.drvPath = evalPath(state, a);

    a = queryAttr(e, "outPath");
    if (!a) throw badTerm("output path missing", e);
    elem.outPath = evalPath(state, a);

    return true;
}


static void parseDerivations(EvalState & state, Expr e, UserEnvElems & elems)
{
    ATermList es;
    UserEnvElem elem;

    e = evalExpr(state, e);

    if (parseDerivation(state, e, elem)) 
        elems[elem.outPath] = elem;

    else if (matchAttrs(e, es)) {
        ATermMap drvMap;
        queryAllAttrs(e, drvMap);
        for (ATermIterator i(drvMap.keys()); i; ++i) {
            debug(format("evaluating attribute `%1%'") % *i);
            if (parseDerivation(state, drvMap.get(*i), elem))
                elems[elem.outPath] = elem;
            else
                parseDerivations(state, drvMap.get(*i), elems);
        }
    }

    else if (matchList(e, es)) {
        for (ATermIterator i(es); i; ++i) {
            debug(format("evaluating list element"));
            if (parseDerivation(state, *i, elem))
                elems[elem.outPath] = elem;
            else
                parseDerivations(state, *i, elems);
        }
    }
}


static void loadDerivations(EvalState & state, Path nixExprPath,
    string systemFilter, UserEnvElems & elems)
{
    parseDerivations(state,
        parseExprFromFile(state, absPath(nixExprPath)), elems);

    /* Filter out all derivations not applicable to the current
       system. */
    for (UserEnvElems::iterator i = elems.begin(), j; i != elems.end(); i = j) {
        j = i; j++;
        if (systemFilter != "*" && i->second.system != systemFilter)
            elems.erase(i);
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


static UserEnvElems queryInstalled(EvalState & state, const Path & userEnv)
{
    Path path = userEnv + "/manifest";

    if (!pathExists(path))
        return UserEnvElems(); /* not an error, assume nothing installed */

    Expr e = ATreadFromNamedFile(path.c_str());
    if (!e) throw Error(format("cannot read Nix expression from `%1%'") % path);

    /* Compatibility: Bind(x, y) -> Bind(x, y, NoPos). */
    AddPos addPos;
    e = bottomupRewrite(addPos, e);

    UserEnvElems elems;
    parseDerivations(state, e, elems);
    return elems;
}


static void createUserEnv(EvalState & state, const UserEnvElems & elems,
    const Path & profile, bool keepDerivations)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    PathSet drvsToBuild;
    for (UserEnvElems::const_iterator i = elems.begin(); 
         i != elems.end(); ++i)
        /* Call to `isDerivation' is for compatibility with Nix <= 0.7
           user environments. */
        if (i->second.drvPath != "" && isDerivation(i->second.drvPath))
            drvsToBuild.insert(i->second.drvPath);

    debug(format("building user environment dependencies"));
    buildDerivations(drvsToBuild);

    /* Get the environment builder expression. */
    Expr envBuilder = parseExprFromFile(state,
        nixDataDir + "/nix/corepkgs/buildenv"); /* !!! */

    /* Construct the whole top level derivation. */
    PathSet references;
    ATermList manifest = ATempty;
    ATermList inputs = ATempty;
    for (UserEnvElems::const_iterator i = elems.begin(); 
         i != elems.end(); ++i)
    {
        Path drvPath = keepDerivations ? i->second.drvPath : "";
        ATerm t = makeAttrs(ATmakeList5(
            makeBind(toATerm("type"),
                makeStr(toATerm("derivation")), makeNoPos()),
            makeBind(toATerm("name"),
                makeStr(toATerm(i->second.name)), makeNoPos()),
            makeBind(toATerm("system"),
                makeStr(toATerm(i->second.system)), makeNoPos()),
            makeBind(toATerm("drvPath"),
                makePath(toATerm(drvPath)), makeNoPos()),
            makeBind(toATerm("outPath"),
                makePath(toATerm(i->second.outPath)), makeNoPos())
            ));
        manifest = ATinsert(manifest, t);
        inputs = ATinsert(inputs, makeStr(toATerm(i->second.outPath)));

        /* This is only necessary when installing store paths, e.g.,
           `nix-env -i /nix/store/abcd...-foo'. */
        addTempRoot(i->second.outPath);
        ensurePath(i->second.outPath);
        
        references.insert(i->second.outPath);
        if (drvPath != "") references.insert(drvPath);
    }

    /* Also write a copy of the list of inputs to the store; we need
       it for future modifications of the environment. */
    Path manifestFile = addTextToStore("env-manifest",
        atPrint(makeList(ATreverse(manifest))), references);

    Expr topLevel = makeCall(envBuilder, makeAttrs(ATmakeList3(
        makeBind(toATerm("system"),
            makeStr(toATerm(thisSystem)), makeNoPos()),
        makeBind(toATerm("derivations"),
            makeList(ATreverse(inputs)), makeNoPos()),
        makeBind(toATerm("manifest"),
            makeAttrs(ATmakeList2(
                makeBind(toATerm("type"),
                    makeStr(toATerm("storePath")), makeNoPos()),
                makeBind(toATerm("outPath"),
                    makePath(toATerm(manifestFile)), makeNoPos())
                )), makeNoPos())
        )));

    /* Instantiate it. */
    debug(format("evaluating builder expression `%1%'") % topLevel);
    UserEnvElem topLevelDrv;
    if (!parseDerivation(state, topLevel, topLevelDrv))
        abort();
    
    /* Realise the resulting store expression. */
    debug(format("building user environment"));
    buildDerivations(singleton<PathSet>(topLevelDrv.drvPath));

    /* Switch the current user environment to the output path. */
    debug(format("switching to new user environment"));
    Path generation = createGeneration(profile, topLevelDrv.outPath);
    switchLink(profile, generation);
}


static UserEnvElems filterBySelector(const UserEnvElems & allElems,
    const Strings & args)
{
    DrvNames selectors = drvNamesFromArgs(args);

    UserEnvElems elems;

    /* Filter out the ones we're not interested in. */
    for (UserEnvElems::const_iterator i = allElems.begin();
         i != allElems.end(); ++i)
    {
        DrvName drvName(i->second.name);
        for (DrvNames::iterator j = selectors.begin();
             j != selectors.end(); ++j)
        {
            if (j->matches(drvName)) {
                j->hits++;
                elems.insert(*i);
            }
        }
    }
            
    /* Check that all selectors have been used. */
    for (DrvNames::iterator i = selectors.begin();
         i != selectors.end(); ++i)
        if (i->hits == 0)
            throw Error(format("selector `%1%' matches no derivations")
                % i->fullName);

    return elems;
}


static void queryInstSources(EvalState & state,
    const InstallSourceInfo & instSource, const Strings & args,
    UserEnvElems & elems)
{
    InstallSourceType type = instSource.type;
    if (type == srcUnknown && args.size() > 0 && args.front()[0] == '/')
        type = srcStorePaths;
    
    switch (type) {

        /* Get the available user environment elements from the
           derivations specified in a Nix expression, including only
           those with names matching any of the names in `args'. */
        case srcUnknown:
        case srcNixExprDrvs: {

            /* Load the derivations from the (default or specified)
               Nix expression. */
            UserEnvElems allElems;
            loadDerivations(state, instSource.nixExprPath,
                instSource.systemFilter, allElems);

            elems = filterBySelector(allElems, args);
    
            break;
        }

        /* Get the available user environment elements from the Nix
           expressions specified on the command line; these should be
           functions that take the default Nix expression file as
           argument, e.g., if the file is `./foo.nix', then the
           argument `x: x.bar' is equivalent to `(x: x.bar)
           (import ./foo.nix)' = `(import ./foo.nix).bar'. */
        case srcNixExprs: {
                

            Expr e1 = parseExprFromFile(state,
                absPath(instSource.nixExprPath));

            for (Strings::const_iterator i = args.begin();
                 i != args.end(); ++i)
            {
                Expr e2 = parseExprFromString(state, *i, absPath("."));
                Expr call = makeCall(e2, e1);
                parseDerivations(state, call, elems);
            }
            
            break;
        }
            
        /* The available user environment elements are specified as a
           list of store paths (which may or may not be
           derivations). */
        case srcStorePaths: {

            for (Strings::const_iterator i = args.begin();
                 i != args.end(); ++i)
            {
                assertStorePath(*i);

                UserEnvElem elem;
                string name = baseNameOf(*i);
                unsigned int dash = name.find('-');
                if (dash != string::npos)
                    name = string(name, dash + 1);

                if (isDerivation(*i)) {
                    elem.drvPath = *i;
                    elem.outPath = findOutput(derivationFromPath(*i), "out");
                    if (name.size() >= drvExtension.size() &&
                        string(name, name.size() - drvExtension.size()) == drvExtension)
                        name = string(name, 0, name.size() - drvExtension.size());
                }
                else elem.outPath = *i;

                elem.name = name;

                elems[elem.outPath] = elem;
            }
            
            break;
        }
            
        /* Get the available user environment elements from another
           user environment.  These are then filtered as in the
           `srcNixExprDrvs' case. */
        case srcProfile: {
            elems = filterBySelector(
                queryInstalled(state, instSource.profile), args);
            break;
        }
    }
}


static void installDerivations(Globals & globals,
    const Strings & args, const Path & profile)
{
    debug(format("installing derivations"));

    /* Get the set of user environment elements to be installed. */
    UserEnvElems newElems;
    queryInstSources(globals.state, globals.instSource, args, newElems);

    StringSet newNames;
    for (UserEnvElems::iterator i = newElems.begin(); i != newElems.end(); ++i) {
        printMsg(lvlInfo,
            format("installing `%1%'") % i->second.name);
        newNames.insert(DrvName(i->second.name).name);
    }

    /* Add in the already installed derivations, unless they have the
       same name as a to-be-installed element. */
    UserEnvElems installedElems = queryInstalled(globals.state, profile);

    for (UserEnvElems::iterator i = installedElems.begin();
         i != installedElems.end(); ++i)
    {
        DrvName drvName(i->second.name);
        if (!globals.preserveInstalled &&
            newNames.find(drvName.name) != newNames.end())
            printMsg(lvlInfo,
                format("uninstalling `%1%'") % i->second.name);
        else
            newElems.insert(*i);
    }

    if (globals.dryRun) return;

    createUserEnv(globals.state, newElems,
        profile, globals.keepDerivations);
}


static void opInstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    installDerivations(globals, opArgs, globals.profile);
}


typedef enum { utLt, utLeq, utAlways } UpgradeType;


static void upgradeDerivations(Globals & globals,
    const Strings & args, const Path & profile,
    UpgradeType upgradeType)
{
    debug(format("upgrading derivations"));

    /* Upgrade works as follows: we take all currently installed
       derivations, and for any derivation matching any selector, look
       for a derivation in the input Nix expression that has the same
       name and a higher version number. */

    /* Load the currently installed derivations. */
    UserEnvElems installedElems = queryInstalled(globals.state, profile);

    /* Fetch all derivations from the input file. */
    UserEnvElems availElems;
    queryInstSources(globals.state, globals.instSource, args, availElems);

    /* Go through all installed derivations. */
    UserEnvElems newElems;
    for (UserEnvElems::iterator i = installedElems.begin();
         i != installedElems.end(); ++i)
    {
        DrvName drvName(i->second.name);

        /* Find the derivation in the input Nix expression with the
           same name and satisfying the version constraints specified
           by upgradeType.  If there are multiple matches, take the
           one with highest version. */
        UserEnvElems::iterator bestElem = availElems.end();
        DrvName bestName;
        for (UserEnvElems::iterator j = availElems.begin();
             j != availElems.end(); ++j)
        {
            DrvName newName(j->second.name);
            if (newName.name == drvName.name) {
                int d = compareVersions(drvName.version, newName.version);
                if (upgradeType == utLt && d < 0 ||
                    upgradeType == utLeq && d <= 0 ||
                    upgradeType == utAlways)
                {
                    if ((bestElem == availElems.end() ||
                         compareVersions(
                             bestName.version, newName.version) < 0))
                    {
                        bestElem = j;
                        bestName = newName;
                    }
                }
            }
        }

        if (bestElem != availElems.end() &&
            i->second.outPath != bestElem->second.outPath)
        {
            printMsg(lvlInfo,
                format("upgrading `%1%' to `%2%'")
                % i->second.name % bestElem->second.name);
            newElems.insert(*bestElem);
        } else newElems.insert(*i);
    }
    
    if (globals.dryRun) return;

    createUserEnv(globals.state, newElems,
        profile, globals.keepDerivations);
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

    upgradeDerivations(globals, opArgs, globals.profile, upgradeType);
}


static void uninstallDerivations(Globals & globals, DrvNames & selectors,
    Path & profile)
{
    UserEnvElems installedElems = queryInstalled(globals.state, profile);

    for (UserEnvElems::iterator i = installedElems.begin();
         i != installedElems.end(); ++i)
    {
        DrvName drvName(i->second.name);
        for (DrvNames::iterator j = selectors.begin();
             j != selectors.end(); ++j)
            if (j->matches(drvName)) {
                printMsg(lvlInfo,
                    format("uninstalling `%1%'") % i->second.name);
                installedElems.erase(i);
            }
    }

    if (globals.dryRun) return;

    createUserEnv(globals.state, installedElems,
        profile, globals.keepDerivations);
}


static void opUninstall(Globals & globals,
    Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flags `%1%'") % opFlags.front());

    DrvNames drvNames = drvNamesFromArgs(opArgs);

    uninstallDerivations(globals, drvNames,
        globals.profile);
}


static bool cmpChars(char a, char b)
{
    return toupper(a) < toupper(b);
}


static bool cmpElemByName(const UserEnvElem & a, const UserEnvElem & b)
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
    bool printOutPath = false;

    enum { sInstalled, sAvailable } source = sInstalled;

    readOnlyMode = true; /* makes evaluation a bit faster */

    for (Strings::iterator i = opFlags.begin();
         i != opFlags.end(); ++i)
        if (*i == "--status" || *i == "-s") printStatus = true;
        else if (*i == "--no-name") printName = false;
        else if (*i == "--system") printSystem = true;
        else if (*i == "--drv-path") printDrvPath = true;
        else if (*i == "--out-path") printOutPath = true;
        else if (*i == "--installed") source = sInstalled;
        else if (*i == "--available" || *i == "-a") source = sAvailable;
        else throw UsageError(format("unknown flag `%1%'") % *i);

    /* Obtain derivation information from the specified source. */
    UserEnvElems elems;

    switch (source) {

        case sInstalled:
            elems = queryInstalled(globals.state, globals.profile);
            break;

        case sAvailable: {
            loadDerivations(globals.state, globals.instSource.nixExprPath,
                globals.instSource.systemFilter, elems);
            break;
        }

        default: abort();
    }

    if (opArgs.size() != 0) throw UsageError("no arguments expected");

    /* Sort them by name. */
    vector<UserEnvElem> elems2;
    for (UserEnvElems::iterator i = elems.begin(); i != elems.end(); ++i)
        elems2.push_back(i->second);
    sort(elems2.begin(), elems2.end(), cmpElemByName);

    /* We only need to know the installed paths when we are querying
       the status of the derivation. */
    UserEnvElems installed; /* installed paths */
    
    if (printStatus)
        installed = queryInstalled(globals.state, globals.profile);
            
    /* Print the desired columns. */
    Table table;
    
    for (vector<UserEnvElem>::iterator i = elems2.begin();
         i != elems2.end(); ++i)
    {
        Strings columns;
        
        if (printStatus) {
            Substitutes subs = querySubstitutes(noTxn, i->drvPath);
            columns.push_back(
                (string) (installed.find(i->outPath)
                    != installed.end() ? "I" : "-")
                + (isValidPath(i->outPath) ? "P" : "-")
                + (subs.size() > 0 ? "S" : "-"));
        }

        if (printName) columns.push_back(i->name);

        if (printSystem) columns.push_back(i->system);

        if (printDrvPath) columns.push_back(i->drvPath == "" ? "-" : i->drvPath);
        
        if (printOutPath) columns.push_back(i->outPath);

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


static string needArg(Strings::iterator & i,
    const Strings & args, const string & arg)
{
    ++i;
    if (i == args.end()) throw UsageError(
        format("`%1%' requires an argument") % arg);
    return *i;
}


void run(Strings args)
{
    Strings opFlags, opArgs;
    Operation op = 0;
    
    Globals globals;
    
    globals.instSource.type = srcUnknown;
    globals.instSource.nixExprPath = getDefNixExprPath();
    globals.instSource.systemFilter = thisSystem;
    
    globals.dryRun = false;
    globals.preserveInstalled = false;

    globals.keepDerivations =
        queryBoolSetting("env-keep-derivations", false);
    
    for (Strings::iterator i = args.begin(); i != args.end(); ++i) {
        string arg = *i;

        Operation oldOp = op;

        if (arg == "--install" || arg == "-i")
            op = opInstall;
        else if (arg == "--from-expression" || arg == "-E")
            globals.instSource.type = srcNixExprs;
        else if (arg == "--from-profile") {
            globals.instSource.type = srcProfile;
            globals.instSource.profile = needArg(i, args, arg);
        }
        else if (arg == "--uninstall" || arg == "-e")
            op = opUninstall;
        else if (arg == "--upgrade" || arg == "-u")
            op = opUpgrade;
        else if (arg == "--query" || arg == "-q")
            op = opQuery;
        else if (arg == "--import" || arg == "-I") /* !!! bad name */
            op = opDefaultExpr;
        else if (arg == "--profile" || arg == "-p") {
            globals.profile = absPath(needArg(i, args, arg));
        }
        else if (arg == "--file" || arg == "-f") {
            globals.instSource.nixExprPath = absPath(needArg(i, args, arg));
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
            globals.instSource.systemFilter = needArg(i, args, arg);
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
