#include "profiles.hh"
#include "names.hh"
#include "globals.hh"
#include "misc.hh"
#include "shared.hh"
#include "parser.hh"
#include "eval.hh"
#include "help.txt.hh"
#include "get-drvs.hh"
#include "attr-path.hh"
#include "common-opts.hh"
#include "xml-writer.hh"
#include "store-api.hh"
#include "user-env.hh"
#include "util.hh"

#include <cerrno>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


using namespace nix;
using std::cout;


typedef enum {
    srcNixExprDrvs,
    srcNixExprs,
    srcStorePaths,
    srcProfile,
    srcAttrPath,
    srcUnknown
} InstallSourceType;


struct InstallSourceInfo
{
    InstallSourceType type;
    Path nixExprPath; /* for srcNixExprDrvs, srcNixExprs */
    Path profile; /* for srcProfile */
    string systemFilter; /* for srcNixExprDrvs */
    bool prebuiltOnly;
    Bindings autoArgs;
    InstallSourceInfo() : prebuiltOnly(false) { };
};


struct Globals
{
    InstallSourceInfo instSource;
    Path profile;
    EvalState state;
    bool dryRun;
    bool preserveInstalled;
    bool keepDerivations;
    string forceName;
};


typedef void (* Operation) (Globals & globals,
    Strings args, Strings opFlags, Strings opArgs);


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


static string needArg(Strings::iterator & i,
    Strings & args, const string & arg)
{
    if (i == args.end()) throw UsageError(
        format("`%1%' requires an argument") % arg);
    return *i++;
}


static bool parseInstallSourceOptions(Globals & globals,
    Strings::iterator & i, Strings & args, const string & arg)
{
    if (arg == "--from-expression" || arg == "-E")
        globals.instSource.type = srcNixExprs;
    else if (arg == "--from-profile") {
        globals.instSource.type = srcProfile;
        globals.instSource.profile = needArg(i, args, arg);
    }
    else if (arg == "--attr" || arg == "-A")
        globals.instSource.type = srcAttrPath;
    else if (arg == "--prebuilt-only" || arg == "-b")
        globals.instSource.prebuiltOnly = true;
    else return false;
    return true;
}


static bool isNixExpr(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == -1)
        throw SysError(format("getting information about `%1%'") % path);

    return !S_ISDIR(st.st_mode) || pathExists(path + "/default.nix");
}


static void getAllExprs(EvalState & state,
    const Path & path, ExprAttrs & attrs)
{
    Strings names = readDirectory(path);
    StringSet namesSorted(names.begin(), names.end());

    foreach (StringSet::iterator, i, namesSorted) {
        Path path2 = path + "/" + *i;
        
        struct stat st;
        if (stat(path2.c_str(), &st) == -1)
            continue; // ignore dangling symlinks in ~/.nix-defexpr
        
        if (isNixExpr(path2)) {
            /* Strip off the `.nix' filename suffix (if applicable),
               otherwise the attribute cannot be selected with the
               `-A' option.  Useful if you want to stick a Nix
               expression directly in ~/.nix-defexpr. */
            string attrName = *i;
            if (hasSuffix(attrName, ".nix"))
                attrName = string(attrName, 0, attrName.size() - 4);
            attrs.attrs[state.symbols.create(attrName)] =
                ExprAttrs::Attr(parseExprFromFile(state, absPath(path2)), noPos);
        }
        else
            /* `path2' is a directory (with no default.nix in it);
               recurse into it. */
            getAllExprs(state, path2, attrs);
    }
}


static Expr * loadSourceExpr(EvalState & state, const Path & path)
{
    if (isNixExpr(path)) return parseExprFromFile(state, absPath(path));

    /* The path is a directory.  Put the Nix expressions in the
       directory in an attribute set, with the file name of each
       expression as the attribute name.  Recurse into subdirectories
       (but keep the attribute set flat, not nested, to make it easier
       for a user to have a ~/.nix-defexpr directory that includes
       some system-wide directory). */
    ExprAttrs * attrs = new ExprAttrs;
    attrs->attrs[state.symbols.create("_combineChannels")] =
        ExprAttrs::Attr(new ExprList(), noPos);
    getAllExprs(state, path, *attrs);
    return attrs;
}


static void loadDerivations(EvalState & state, Path nixExprPath,
    string systemFilter, const Bindings & autoArgs,
    const string & pathPrefix, DrvInfos & elems)
{
    Value v;
    findAlongAttrPath(state, pathPrefix, autoArgs, loadSourceExpr(state, nixExprPath), v);
    
    getDerivations(state, v, pathPrefix, autoArgs, elems);

    /* Filter out all derivations not applicable to the current
       system. */
    for (DrvInfos::iterator i = elems.begin(), j; i != elems.end(); i = j) {
        j = i; j++;
        if (systemFilter != "*" && i->system != systemFilter)
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


static int getPriority(EvalState & state, const DrvInfo & drv)
{
    MetaValue value = drv.queryMetaInfo(state, "priority");
    int prio = 0;
    if (value.type == MetaValue::tpInt) prio = value.intValue;
    else if (value.type == MetaValue::tpString)
        /* Backwards compatibility.  Priorities used to be strings
           before we had support for integer meta field. */
        string2Int(value.stringValue, prio);
    return prio;
}


static int comparePriorities(EvalState & state,
    const DrvInfo & drv1, const DrvInfo & drv2)
{
    return getPriority(state, drv2) - getPriority(state, drv1);
}


static bool isPrebuilt(EvalState & state, const DrvInfo & elem)
{
    return
        store->isValidPath(elem.queryOutPath(state)) ||
        store->hasSubstitutes(elem.queryOutPath(state));
}


static DrvInfos filterBySelector(EvalState & state, const DrvInfos & allElems,
    const Strings & args, bool newestOnly, bool prebuiltOnly)
{
    DrvNames selectors = drvNamesFromArgs(args);

    DrvInfos elems;
    set<unsigned int> done;

    for (DrvNames::iterator i = selectors.begin();
         i != selectors.end(); ++i)
    {
        typedef list<std::pair<DrvInfo, unsigned int> > Matches;
        Matches matches;
        unsigned int n = 0;
        for (DrvInfos::const_iterator j = allElems.begin();
             j != allElems.end(); ++j, ++n)
        {
            DrvName drvName(j->name);
            if (i->matches(drvName)) {
                i->hits++;
                if (!prebuiltOnly || isPrebuilt(state, *j))
                    matches.push_back(std::pair<DrvInfo, unsigned int>(*j, n));
            }
        }

        /* If `newestOnly', if a selector matches multiple derivations
           with the same name, pick the one matching the current
           system.  If there are still multiple derivations, pick the
           one with the highest priority.  If there are still multiple
           derivations, pick the one with the highest version.
           Finally, if there are still multiple derivations,
           arbitrarily pick the first one. */
        if (newestOnly) {

            /* Map from package names to derivations. */
            typedef map<string, std::pair<DrvInfo, unsigned int> > Newest;
            Newest newest;
            StringSet multiple;

            for (Matches::iterator j = matches.begin(); j != matches.end(); ++j) {
                DrvName drvName(j->first.name);
                int d = 1;

                Newest::iterator k = newest.find(drvName.name);
                
                if (k != newest.end()) {
                    d = j->first.system == k->second.first.system ? 0 :
                        j->first.system == thisSystem ? 1 :
                        k->second.first.system == thisSystem ? -1 : 0;
                    printMsg(lvlError, format("%1% %2% %3% %4%") % j->first.system % k->second.first.system % thisSystem % d);
                    if (d == 0)
                        d = comparePriorities(state, j->first, k->second.first);
                    if (d == 0)
                        d = compareVersions(drvName.version, DrvName(k->second.first.name).version);
                }

                if (d > 0) {
                    newest[drvName.name] = *j;
                    multiple.erase(j->first.name);
                } else if (d == 0) {
                    multiple.insert(j->first.name);
                }
            }

            matches.clear();
            for (Newest::iterator j = newest.begin(); j != newest.end(); ++j) {
                if (multiple.find(j->second.first.name) != multiple.end())
                    printMsg(lvlInfo,
                        format("warning: there are multiple derivations named `%1%'; using the first one")
                        % j->second.first.name);
                matches.push_back(j->second);
            }
        }

        /* Insert only those elements in the final list that we
           haven't inserted before. */
        for (Matches::iterator j = matches.begin(); j != matches.end(); ++j)
            if (done.find(j->second) == done.end()) {
                done.insert(j->second);
                elems.push_back(j->first);
            }
    }
            
    /* Check that all selectors have been used. */
    for (DrvNames::iterator i = selectors.begin();
         i != selectors.end(); ++i)
        if (i->hits == 0 && i->fullName != "*")
            throw Error(format("selector `%1%' matches no derivations")
                % i->fullName);

    return elems;
}


static bool isPath(const string & s)
{
    return s.find('/') != string::npos;
}


static void queryInstSources(EvalState & state,
    const InstallSourceInfo & instSource, const Strings & args,
    DrvInfos & elems, bool newestOnly)
{
    InstallSourceType type = instSource.type;
    if (type == srcUnknown && args.size() > 0 && isPath(args.front()))
        type = srcStorePaths;
    
    switch (type) {

        /* Get the available user environment elements from the
           derivations specified in a Nix expression, including only
           those with names matching any of the names in `args'. */
        case srcUnknown:
        case srcNixExprDrvs: {

            /* Load the derivations from the (default or specified)
               Nix expression. */
            DrvInfos allElems;
            loadDerivations(state, instSource.nixExprPath,
                instSource.systemFilter, instSource.autoArgs, "", allElems);

            elems = filterBySelector(state, allElems, args,
                newestOnly, instSource.prebuiltOnly);
    
            break;
        }

        /* Get the available user environment elements from the Nix
           expressions specified on the command line; these should be
           functions that take the default Nix expression file as
           argument, e.g., if the file is `./foo.nix', then the
           argument `x: x.bar' is equivalent to `(x: x.bar)
           (import ./foo.nix)' = `(import ./foo.nix).bar'. */
        case srcNixExprs: {
                
            Expr * e1 = loadSourceExpr(state, instSource.nixExprPath);

            foreach (Strings::const_iterator, i, args) {
                Expr * e2 = parseExprFromString(state, *i, absPath("."));
                Expr * call = new ExprApp(e2, e1);
                Value v; state.eval(call, v);
                getDerivations(state, v, "", instSource.autoArgs, elems);
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
                Path path = followLinksToStorePath(*i);

                DrvInfo elem;
                elem.attrs = new Bindings;
                string name = baseNameOf(path);
                string::size_type dash = name.find('-');
                if (dash != string::npos)
                    name = string(name, dash + 1);

                if (isDerivation(path)) {
                    elem.setDrvPath(path);
                    elem.setOutPath(findOutput(derivationFromPath(path), "out"));
                    if (name.size() >= drvExtension.size() &&
                        string(name, name.size() - drvExtension.size()) == drvExtension)
                        name = string(name, 0, name.size() - drvExtension.size());
                }
                else elem.setOutPath(path);

                elem.name = name;

                elems.push_back(elem);
            }
            
            break;
        }
            
        /* Get the available user environment elements from another
           user environment.  These are then filtered as in the
           `srcNixExprDrvs' case. */
        case srcProfile: {
            elems = filterBySelector(state,
                queryInstalled(state, instSource.profile),
                args, newestOnly, instSource.prebuiltOnly);
            break;
        }

        case srcAttrPath: {
            foreach (Strings::const_iterator, i, args) {
                Value v;
                findAlongAttrPath(state, *i, instSource.autoArgs,
                    loadSourceExpr(state, instSource.nixExprPath), v);
                getDerivations(state, v, "", instSource.autoArgs, elems);
            }
            break;
        }
    }
}


static void printMissing(EvalState & state, const DrvInfos & elems)
{
    PathSet targets;
    foreach (DrvInfos::const_iterator, i, elems) {
        Path drvPath = i->queryDrvPath(state);
        if (drvPath != "")
            targets.insert(drvPath);
        else
            targets.insert(i->queryOutPath(state));
    }

    printMissing(targets);
}


static bool keep(MetaInfo & meta)
{
    MetaValue value = meta["keep"];
    return value.type == MetaValue::tpString && value.stringValue == "true";
}


static void installDerivations(Globals & globals,
    const Strings & args, const Path & profile)
{
    debug(format("installing derivations"));

    /* Get the set of user environment elements to be installed. */
    DrvInfos newElems;
    queryInstSources(globals.state, globals.instSource, args, newElems, true);

    StringSet newNames;
    for (DrvInfos::iterator i = newElems.begin(); i != newElems.end(); ++i) {
        /* `forceName' is a hack to get package names right in some
           one-click installs, namely those where the name used in the
           path is not the one we want (e.g., `java-front' versus
           `java-front-0.9pre15899'). */
        if (globals.forceName != "")
            i->name = globals.forceName;
        newNames.insert(DrvName(i->name).name);
    }

    /* Add in the already installed derivations, unless they have the
       same name as a to-be-installed element. */

    while (true) {
        string lockToken = optimisticLockProfile(profile);
        
        DrvInfos installedElems = queryInstalled(globals.state, profile);
        
        DrvInfos allElems(newElems);
        foreach (DrvInfos::iterator, i, installedElems) {
            DrvName drvName(i->name);
            MetaInfo meta = i->queryMetaInfo(globals.state);
            if (!globals.preserveInstalled &&
                newNames.find(drvName.name) != newNames.end() &&
                !keep(meta))
                printMsg(lvlInfo,
                    format("replacing old `%1%'") % i->name);
            else
                allElems.push_back(*i);
        }

        foreach (DrvInfos::iterator, i, newElems)
            printMsg(lvlInfo, format("installing `%1%'") % i->name);
    
        printMissing(globals.state, newElems);
    
        if (globals.dryRun) return;

        if (createUserEnv(globals.state, allElems,
                profile, globals.keepDerivations, lockToken)) break;
    }
}


static void opInstall(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else if (arg == "--preserve-installed" || arg == "-P")
            globals.preserveInstalled = true;
        else throw UsageError(format("unknown flag `%1%'") % arg);
    }

    installDerivations(globals, opArgs, globals.profile);
}


typedef enum { utLt, utLeq, utEq, utAlways } UpgradeType;


static void upgradeDerivations(Globals & globals,
    const Strings & args, UpgradeType upgradeType)
{
    debug(format("upgrading derivations"));

    /* Upgrade works as follows: we take all currently installed
       derivations, and for any derivation matching any selector, look
       for a derivation in the input Nix expression that has the same
       name and a higher version number. */

    while (true) {
        string lockToken = optimisticLockProfile(globals.profile);
        
        DrvInfos installedElems = queryInstalled(globals.state, globals.profile);

        /* Fetch all derivations from the input file. */
        DrvInfos availElems;
        queryInstSources(globals.state, globals.instSource, args, availElems, false);

        /* Go through all installed derivations. */
        DrvInfos newElems;
        foreach (DrvInfos::iterator, i, installedElems) {
            DrvName drvName(i->name);

            try {

                MetaInfo meta = i->queryMetaInfo(globals.state);
                if (keep(meta)) {
                    newElems.push_back(*i);
                    continue;
                }

                /* Find the derivation in the input Nix expression
                   with the same name that satisfies the version
                   constraints specified by upgradeType.  If there are
                   multiple matches, take the one with the highest
                   priority.  If there are still multiple matches,
                   take the one with the highest version. */
                DrvInfos::iterator bestElem = availElems.end();
                DrvName bestName;
                foreach (DrvInfos::iterator, j, availElems) {
                    DrvName newName(j->name);
                    if (newName.name == drvName.name) {
                        int d = comparePriorities(globals.state, *i, *j);
                        if (d == 0) d = compareVersions(drvName.version, newName.version);
                        if ((upgradeType == utLt && d < 0) ||
                            (upgradeType == utLeq && d <= 0) ||
                            (upgradeType == utEq && d == 0) ||
                            upgradeType == utAlways)
                        {
                            int d2 = -1;
                            if (bestElem != availElems.end()) {
                                d2 = comparePriorities(globals.state, *bestElem, *j);
                                if (d2 == 0) d2 = compareVersions(bestName.version, newName.version);
                            }
                            if (d2 < 0) {
                                bestElem = j;
                                bestName = newName;
                            }
                        }
                    }
                }
                
                if (bestElem != availElems.end() &&
                    i->queryOutPath(globals.state) !=
                    bestElem->queryOutPath(globals.state))
                {
                    printMsg(lvlInfo,
                        format("upgrading `%1%' to `%2%'")
                        % i->name % bestElem->name);
                    newElems.push_back(*bestElem);
                } else newElems.push_back(*i);

            } catch (Error & e) {
                e.addPrefix(format("while trying to find an upgrade for `%1%':\n") % i->name);
                throw;
            }
        }
    
        printMissing(globals.state, newElems);
    
        if (globals.dryRun) return;

        if (createUserEnv(globals.state, newElems,
                globals.profile, globals.keepDerivations, lockToken)) break;
    }
}


static void opUpgrade(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    UpgradeType upgradeType = utLt;
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else if (arg == "--lt") upgradeType = utLt;
        else if (arg == "--leq") upgradeType = utLeq;
        else if (arg == "--eq") upgradeType = utEq;
        else if (arg == "--always") upgradeType = utAlways;
        else throw UsageError(format("unknown flag `%1%'") % arg);
    }

    upgradeDerivations(globals, opArgs, upgradeType);
}


static void setMetaFlag(EvalState & state, DrvInfo & drv,
    const string & name, const string & value)
{
    MetaInfo meta = drv.queryMetaInfo(state);
    MetaValue v;
    v.type = MetaValue::tpString;
    v.stringValue = value;
    meta[name] = v;
    drv.setMetaInfo(meta);
}


static void opSetFlag(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() < 2)
        throw UsageError("not enough arguments to `--set-flag'");

    Strings::iterator arg = opArgs.begin();
    string flagName = *arg++;
    string flagValue = *arg++;
    DrvNames selectors = drvNamesFromArgs(Strings(arg, opArgs.end()));

    while (true) {
        string lockToken = optimisticLockProfile(globals.profile);

        DrvInfos installedElems = queryInstalled(globals.state, globals.profile);

        /* Update all matching derivations. */
        foreach (DrvInfos::iterator, i, installedElems) {
            DrvName drvName(i->name);
            foreach (DrvNames::iterator, j, selectors)
                if (j->matches(drvName)) {
                    printMsg(lvlInfo,
                        format("setting flag on `%1%'") % i->name);
                    setMetaFlag(globals.state, *i, flagName, flagValue);
                    break;
                }
        }

        /* Write the new user environment. */
        if (createUserEnv(globals.state, installedElems,
                globals.profile, globals.keepDerivations, lockToken)) break;
    }
}


static void opSet(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else throw UsageError(format("unknown flag `%1%'") % arg);
    }

    DrvInfos elems;
    queryInstSources(globals.state, globals.instSource, opArgs, elems, true);

    if (elems.size() != 1)
        throw Error("--set requires exactly one derivation");
    
    DrvInfo & drv(elems.front());

    if (drv.queryDrvPath(globals.state) != "") {
        PathSet paths = singleton<PathSet>(drv.queryDrvPath(globals.state));
        printMissing(paths);
        if (globals.dryRun) return;
        store->buildDerivations(paths);
    }
    else {
        printMissing(singleton<PathSet>(drv.queryOutPath(globals.state)));
        if (globals.dryRun) return;
        store->ensurePath(drv.queryOutPath(globals.state));
    }

    debug(format("switching to new user environment"));
    Path generation = createGeneration(globals.profile, drv.queryOutPath(globals.state));
    switchLink(globals.profile, generation);
}


static void uninstallDerivations(Globals & globals, Strings & selectors,
    Path & profile)
{
    while (true) {
        string lockToken = optimisticLockProfile(profile);

        DrvInfos installedElems = queryInstalled(globals.state, profile);
        DrvInfos newElems;

        foreach (DrvInfos::iterator, i, installedElems) {
            DrvName drvName(i->name);
            bool found = false;
            foreach (Strings::iterator, j, selectors)
                /* !!! the repeated calls to followLinksToStorePath()
                   are expensive, should pre-compute them. */
                if ((isPath(*j) && i->queryOutPath(globals.state) == followLinksToStorePath(*j))
                    || DrvName(*j).matches(drvName))
                {
                    printMsg(lvlInfo, format("uninstalling `%1%'") % i->name);
                    found = true;
                    break;
                }
            if (!found) newElems.push_back(*i);
        }

        if (globals.dryRun) return;

        if (createUserEnv(globals.state, newElems,
                profile, globals.keepDerivations, lockToken)) break;
    }
}


static void opUninstall(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    uninstallDerivations(globals, opArgs, globals.profile);
}


static bool cmpChars(char a, char b)
{
    return toupper(a) < toupper(b);
}


static bool cmpElemByName(const DrvInfo & a, const DrvInfo & b)
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
    
    foreach (Table::iterator, i, table) {
        assert(i->size() == nrColumns);
        Strings::iterator j;
        unsigned int column;
        for (j = i->begin(), column = 0; j != i->end(); ++j, ++column)
            if (j->size() > widths[column]) widths[column] = j->size();
    }

    foreach (Table::iterator, i, table) { 
        Strings::iterator j;
        unsigned int column;
        for (j = i->begin(), column = 0; j != i->end(); ++j, ++column) {
            string s = *j;
            replace(s.begin(), s.end(), '\n', ' ');
            cout << s;
            if (column < nrColumns - 1)
                cout << string(widths[column] - s.size() + 2, ' ');
        }
        cout << std::endl;
    }
}


/* This function compares the version of a element against the
   versions in the given set of elements.  `cvLess' means that only
   lower versions are in the set, `cvEqual' means that at most an
   equal version is in the set, and `cvGreater' means that there is at
   least one element with a higher version in the set.  `cvUnavail'
   means that there are no elements with the same name in the set. */

typedef enum { cvLess, cvEqual, cvGreater, cvUnavail } VersionDiff;

static VersionDiff compareVersionAgainstSet(
    const DrvInfo & elem, const DrvInfos & elems, string & version)
{
    DrvName name(elem.name);
    
    VersionDiff diff = cvUnavail;
    version = "?";
    
    for (DrvInfos::const_iterator i = elems.begin(); i != elems.end(); ++i) {
        DrvName name2(i->name);
        if (name.name == name2.name) {
            int d = compareVersions(name.version, name2.version);
            if (d < 0) {
                diff = cvGreater;
                version = name2.version;
            }
            else if (diff != cvGreater && d == 0) {
                diff = cvEqual;
                version = name2.version;
            }
            else if (diff != cvGreater && diff != cvEqual && d > 0) {
                diff = cvLess;
                if (version == "" || compareVersions(version, name2.version) < 0)
                    version = name2.version;
            }
        }
    }

    return diff;
}


static string colorString(const string & s)
{
    if (!isatty(STDOUT_FILENO)) return s;
    return "\e[1;31m" + s + "\e[0m";
}


static void opQuery(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    typedef vector< map<string, string> > ResultSet;
    Strings remaining;
    string attrPath;
        
    bool printStatus = false;
    bool printName = true;
    bool printAttrPath = false;
    bool printSystem = false;
    bool printDrvPath = false;
    bool printOutPath = false;
    bool printDescription = false;
    bool printMeta = false;
    bool prebuiltOnly = false;
    bool compareVersions = false;
    bool xmlOutput = false;

    enum { sInstalled, sAvailable } source = sInstalled;

    readOnlyMode = true; /* makes evaluation a bit faster */

    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;
        if (arg == "--status" || arg == "-s") printStatus = true;
        else if (arg == "--no-name") printName = false;
        else if (arg == "--system") printSystem = true;
        else if (arg == "--description") printDescription = true;
        else if (arg == "--compare-versions" || arg == "-c") compareVersions = true;
        else if (arg == "--drv-path") printDrvPath = true;
        else if (arg == "--out-path") printOutPath = true;
        else if (arg == "--meta") printMeta = true;
        else if (arg == "--installed") source = sInstalled;
        else if (arg == "--available" || arg == "-a") source = sAvailable;
        else if (arg == "--prebuilt-only" || arg == "-b") prebuiltOnly = true;
        else if (arg == "--xml") xmlOutput = true;
        else if (arg == "--attr-path" || arg == "-P") printAttrPath = true;
        else if (arg == "--attr" || arg == "-A")
            attrPath = needArg(i, args, arg);
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%'") % arg);
        else remaining.push_back(arg);
    }

    if (remaining.size() == 0)
        printMsg(lvlInfo, "warning: you probably meant to specify the argument '*' to show all packages");

    
    /* Obtain derivation information from the specified source. */
    DrvInfos availElems, installedElems;

    if (source == sInstalled || compareVersions || printStatus)
        installedElems = queryInstalled(globals.state, globals.profile);

    if (source == sAvailable || compareVersions)
        loadDerivations(globals.state, globals.instSource.nixExprPath,
            globals.instSource.systemFilter, globals.instSource.autoArgs,
            attrPath, availElems);

    DrvInfos elems = filterBySelector(globals.state,
        source == sInstalled ? installedElems : availElems,
        remaining, false, prebuiltOnly);
    
    DrvInfos & otherElems(source == sInstalled ? availElems : installedElems);

    
    /* Sort them by name. */
    /* !!! */
    vector<DrvInfo> elems2;
    for (DrvInfos::iterator i = elems.begin(); i != elems.end(); ++i)
        elems2.push_back(*i);
    sort(elems2.begin(), elems2.end(), cmpElemByName);

    
    /* We only need to know the installed paths when we are querying
       the status of the derivation. */
    PathSet installed; /* installed paths */
    
    if (printStatus) {
        for (DrvInfos::iterator i = installedElems.begin();
             i != installedElems.end(); ++i)
            installed.insert(i->queryOutPath(globals.state));
    }

    
    /* Print the desired columns, or XML output. */
    Table table;
    std::ostringstream dummy;
    XMLWriter xml(true, *(xmlOutput ? &cout : &dummy));
    XMLOpenElement xmlRoot(xml, "items");
    
    foreach (vector<DrvInfo>::iterator, i, elems2) {
        try {
            startNest(nest, lvlDebug, format("outputting query result `%1%'") % i->attrPath);

            /* For table output. */
            Strings columns;

            /* For XML output. */
            XMLAttrs attrs;

            if (printStatus) {
                bool hasSubs = store->hasSubstitutes(i->queryOutPath(globals.state));
                bool isInstalled = installed.find(i->queryOutPath(globals.state)) != installed.end();
                bool isValid = store->isValidPath(i->queryOutPath(globals.state));
                if (xmlOutput) {
                    attrs["installed"] = isInstalled ? "1" : "0";
                    attrs["valid"] = isValid ? "1" : "0";
                    attrs["substitutable"] = hasSubs ? "1" : "0";
                } else
                    columns.push_back(
                        (string) (isInstalled ? "I" : "-")
                        + (isValid ? "P" : "-")
                        + (hasSubs ? "S" : "-"));
            }

            if (xmlOutput)
                attrs["attrPath"] = i->attrPath;
            else if (printAttrPath)
                columns.push_back(i->attrPath);

            if (xmlOutput)
                attrs["name"] = i->name;
            else if (printName)
                columns.push_back(i->name);

            if (compareVersions) {
                /* Compare this element against the versions of the
                   same named packages in either the set of available
                   elements, or the set of installed elements.  !!!
                   This is O(N * M), should be O(N * lg M). */
                string version;
                VersionDiff diff = compareVersionAgainstSet(*i, otherElems, version);

                char ch;
                switch (diff) {
                    case cvLess: ch = '>'; break;
                    case cvEqual: ch = '='; break;
                    case cvGreater: ch = '<'; break;
                    case cvUnavail: ch = '-'; break;
                    default: abort();
                }

                if (xmlOutput) {
                    if (diff != cvUnavail) {
                        attrs["versionDiff"] = ch;
                        attrs["maxComparedVersion"] = version;
                    }
                } else {
                    string column = (string) "" + ch + " " + version;
                    if (diff == cvGreater) column = colorString(column);
                    columns.push_back(column);
                }
            }

            if (xmlOutput) {
                if (i->system != "") attrs["system"] = i->system;
            }
            else if (printSystem) 
                columns.push_back(i->system);

            if (printDrvPath) {
                string drvPath = i->queryDrvPath(globals.state);
                if (xmlOutput) {
                    if (drvPath != "") attrs["drvPath"] = drvPath;
                } else
                    columns.push_back(drvPath == "" ? "-" : drvPath);
            }
        
            if (printOutPath) {
                string outPath = i->queryOutPath(globals.state);
                if (xmlOutput) {
                    if (outPath != "") attrs["outPath"] = outPath;
                } else
                    columns.push_back(outPath);
            }

            if (printDescription) {
                MetaInfo meta = i->queryMetaInfo(globals.state);
                MetaValue value = meta["description"];
                string descr = value.type == MetaValue::tpString ? value.stringValue : "";
                if (xmlOutput) {
                    if (descr != "") attrs["description"] = descr;
                } else
                    columns.push_back(descr);
            }

            if (xmlOutput)
                if (printMeta) {
                    XMLOpenElement item(xml, "item", attrs);
                    MetaInfo meta = i->queryMetaInfo(globals.state);
                    for (MetaInfo::iterator j = meta.begin(); j != meta.end(); ++j) {
                        XMLAttrs attrs2;
                        attrs2["name"] = j->first;
                        if (j->second.type == MetaValue::tpString) {
                            attrs2["type"] = "string";
                            attrs2["value"] = j->second.stringValue;
                            xml.writeEmptyElement("meta", attrs2);
                        } else if (j->second.type == MetaValue::tpInt) {
                            attrs2["type"] = "int";
                            attrs2["value"] = (format("%1%") % j->second.intValue).str();
                            xml.writeEmptyElement("meta", attrs2);
                        } else if (j->second.type == MetaValue::tpStrings) {
                            attrs2["type"] = "strings";
                            XMLOpenElement m(xml, "meta", attrs2);
                            foreach (Strings::iterator, k, j->second.stringValues) { 
                                XMLAttrs attrs3;
                                attrs3["value"] = *k;
                                xml.writeEmptyElement("string", attrs3);
                           }
                        }
                    }
                }
                else
                    xml.writeEmptyElement("item", attrs);
            else
                table.push_back(columns);

            cout.flush();

        } catch (AssertionError & e) {
            printMsg(lvlTalkative, format("skipping derivation named `%1%' which gives an assertion failure") % i->name);
        }
    }

    if (!xmlOutput) printTable(table);
}


static void opSwitchProfile(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("exactly one argument expected"));

    Path profile = absPath(opArgs.front());
    Path profileLink = getHomeDir() + "/.nix-profile";

    switchLink(profileLink, profile);
}


static const int prevGen = -2;


static void switchGeneration(Globals & globals, int dstGen)
{
    PathLocks lock;
    lockProfile(lock, globals.profile);
    
    int curGen;
    Generations gens = findGenerations(globals.profile, curGen);

    Generation dst;
    for (Generations::iterator i = gens.begin(); i != gens.end(); ++i)
        if ((dstGen == prevGen && i->number < curGen) ||
            (dstGen >= 0 && i->number == dstGen))
            dst = *i;

    if (!dst) {
        if (dstGen == prevGen)
            throw Error(format("no generation older than the current (%1%) exists")
                % curGen);
        else
            throw Error(format("generation %1% does not exist") % dstGen);
    }

    printMsg(lvlInfo, format("switching from generation %1% to %2%")
        % curGen % dst.number);
    
    if (globals.dryRun) return;
    
    switchLink(globals.profile, dst.path);
}


static void opSwitchGeneration(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
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
    Strings args, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError(format("no arguments expected"));

    switchGeneration(globals, prevGen);
}


static void opListGenerations(Globals & globals,
    Strings args, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError(format("no arguments expected"));

    PathLocks lock;
    lockProfile(lock, globals.profile);
    
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
    Strings args, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag `%1%'") % opFlags.front());

    PathLocks lock;
    lockProfile(lock, globals.profile);
    
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


void run(Strings args)
{
    Strings opFlags, opArgs, remaining;
    Operation op = 0;
    
    Globals globals;
    
    globals.instSource.type = srcUnknown;
    globals.instSource.nixExprPath = getDefNixExprPath();
    globals.instSource.systemFilter = "*";
    
    globals.dryRun = false;
    globals.preserveInstalled = false;

    globals.keepDerivations =
        queryBoolSetting("env-keep-derivations", false);
    
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;

        if (arg == "--install" || arg == "-i")
            op = opInstall;
        else if (parseOptionArg(arg, i, args.end(),
                     globals.state, globals.instSource.autoArgs))
            ;
        else if (arg == "--force-name") // undocumented flag for nix-install-package
            globals.forceName = needArg(i, args, arg);
        else if (arg == "--uninstall" || arg == "-e")
            op = opUninstall;
        else if (arg == "--upgrade" || arg == "-u")
            op = opUpgrade;
        else if (arg == "--set-flag")
            op = opSetFlag;
        else if (arg == "--set")
            op = opSet;
        else if (arg == "--query" || arg == "-q")
            op = opQuery;
        else if (arg == "--profile" || arg == "-p")
            globals.profile = absPath(needArg(i, args, arg));
        else if (arg == "--file" || arg == "-f")
            globals.instSource.nixExprPath = absPath(needArg(i, args, arg));
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
        else if (arg == "--system-filter")
            globals.instSource.systemFilter = needArg(i, args, arg);
        else {
            remaining.push_back(arg);
            if (arg[0] == '-') {
                opFlags.push_back(arg);
                if (arg == "--from-profile") { /* !!! hack */
                    if (i != args.end()) opFlags.push_back(*i++);
                }
            } else opArgs.push_back(arg);
        }

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
    
    store = openStore();

    op(globals, remaining, opFlags, opArgs);

    globals.state.printStats();
}


string programId = "nix-env";
