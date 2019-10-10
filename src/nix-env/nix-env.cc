#include "attr-path.hh"
#include "common-eval-args.hh"
#include "derivations.hh"
#include "eval.hh"
#include "get-drvs.hh"
#include "globals.hh"
#include "names.hh"
#include "profiles.hh"
#include "shared.hh"
#include "store-api.hh"
#include "user-env.hh"
#include "util.hh"
#include "json.hh"
#include "value-to-json.hh"
#include "xml-writer.hh"
#include "legacy.hh"

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
    Bindings * autoArgs;
};


struct Globals
{
    InstallSourceInfo instSource;
    Path profile;
    std::shared_ptr<EvalState> state;
    bool dryRun;
    bool preserveInstalled;
    bool removeAll;
    string forceName;
    bool prebuiltOnly;
};


typedef void (* Operation) (Globals & globals,
    Strings opFlags, Strings opArgs);


static string needArg(Strings::iterator & i,
    Strings & args, const string & arg)
{
    if (i == args.end()) throw UsageError(
        format("'%1%' requires an argument") % arg);
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
    else return false;
    return true;
}


static bool isNixExpr(const Path & path, struct stat & st)
{
    return S_ISREG(st.st_mode) || (S_ISDIR(st.st_mode) && pathExists(path + "/default.nix"));
}


static void getAllExprs(EvalState & state,
    const Path & path, StringSet & attrs, Value & v)
{
    StringSet namesSorted;
    for (auto & i : readDirectory(path)) namesSorted.insert(i.name);

    for (auto & i : namesSorted) {
        /* Ignore the manifest.nix used by profiles.  This is
           necessary to prevent it from showing up in channels (which
           are implemented using profiles). */
        if (i == "manifest.nix") continue;

        Path path2 = path + "/" + i;

        struct stat st;
        if (stat(path2.c_str(), &st) == -1)
            continue; // ignore dangling symlinks in ~/.nix-defexpr

        if (isNixExpr(path2, st) && (!S_ISREG(st.st_mode) || hasSuffix(path2, ".nix"))) {
            /* Strip off the `.nix' filename suffix (if applicable),
               otherwise the attribute cannot be selected with the
               `-A' option.  Useful if you want to stick a Nix
               expression directly in ~/.nix-defexpr. */
            string attrName = i;
            if (hasSuffix(attrName, ".nix"))
                attrName = string(attrName, 0, attrName.size() - 4);
            if (attrs.find(attrName) != attrs.end()) {
                printError(format("warning: name collision in input Nix expressions, skipping '%1%'") % path2);
                continue;
            }
            attrs.insert(attrName);
            /* Load the expression on demand. */
            Value & vFun = state.getBuiltin("import");
            Value & vArg(*state.allocValue());
            mkString(vArg, path2);
            if (v.attrs->size() == v.attrs->capacity())
                throw Error(format("too many Nix expressions in directory '%1%'") % path);
            mkApp(*state.allocAttr(v, state.symbols.create(attrName)), vFun, vArg);
        }
        else if (S_ISDIR(st.st_mode))
            /* `path2' is a directory (with no default.nix in it);
               recurse into it. */
            getAllExprs(state, path2, attrs, v);
    }
}


static void loadSourceExpr(EvalState & state, const Path & path, Value & v)
{
    struct stat st;
    if (stat(path.c_str(), &st) == -1)
        throw SysError(format("getting information about '%1%'") % path);

    if (isNixExpr(path, st))
        state.evalFile(path, v);

    /* The path is a directory.  Put the Nix expressions in the
       directory in a set, with the file name of each expression as
       the attribute name.  Recurse into subdirectories (but keep the
       set flat, not nested, to make it easier for a user to have a
       ~/.nix-defexpr directory that includes some system-wide
       directory). */
    else if (S_ISDIR(st.st_mode)) {
        state.mkAttrs(v, 1024);
        state.mkList(*state.allocAttr(v, state.symbols.create("_combineChannels")), 0);
        StringSet attrs;
        getAllExprs(state, path, attrs, v);
        v.attrs->sort();
    }

    else throw Error("path '%s' is not a directory or a Nix expression", path);
}


static void loadDerivations(EvalState & state, Path nixExprPath,
    string systemFilter, Bindings & autoArgs,
    const string & pathPrefix, DrvInfos & elems)
{
    Value vRoot;
    loadSourceExpr(state, nixExprPath, vRoot);

    Value & v(*findAlongAttrPath(state, pathPrefix, autoArgs, vRoot));

    getDerivations(state, v, pathPrefix, autoArgs, elems, true);

    /* Filter out all derivations not applicable to the current
       system. */
    for (DrvInfos::iterator i = elems.begin(), j; i != elems.end(); i = j) {
        j = i; j++;
        if (systemFilter != "*" && i->querySystem() != systemFilter)
            elems.erase(i);
    }
}


static long getPriority(EvalState & state, DrvInfo & drv)
{
    return drv.queryMetaInt("priority", 0);
}


static long comparePriorities(EvalState & state, DrvInfo & drv1, DrvInfo & drv2)
{
    return getPriority(state, drv2) - getPriority(state, drv1);
}


// FIXME: this function is rather slow since it checks a single path
// at a time.
static bool isPrebuilt(EvalState & state, DrvInfo & elem)
{
    Path path = elem.queryOutPath();
    if (state.store->isValidPath(path)) return true;
    PathSet ps = state.store->querySubstitutablePaths({path});
    return ps.find(path) != ps.end();
}


static void checkSelectorUse(DrvNames & selectors)
{
    /* Check that all selectors have been used. */
    for (auto & i : selectors)
        if (i.hits == 0 && i.fullName != "*")
            throw Error(format("selector '%1%' matches no derivations") % i.fullName);
}


static DrvInfos filterBySelector(EvalState & state, const DrvInfos & allElems,
    const Strings & args, bool newestOnly)
{
    DrvNames selectors = drvNamesFromArgs(args);
    if (selectors.empty())
        selectors.push_back(DrvName("*"));

    DrvInfos elems;
    set<unsigned int> done;

    for (auto & i : selectors) {
        typedef list<std::pair<DrvInfo, unsigned int> > Matches;
        Matches matches;
        unsigned int n = 0;
        for (DrvInfos::const_iterator j = allElems.begin();
             j != allElems.end(); ++j, ++n)
        {
            DrvName drvName(j->queryName());
            if (i.matches(drvName)) {
                i.hits++;
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

            for (auto & j : matches) {
                DrvName drvName(j.first.queryName());
                long d = 1;

                Newest::iterator k = newest.find(drvName.name);

                if (k != newest.end()) {
                    d = j.first.querySystem() == k->second.first.querySystem() ? 0 :
                        j.first.querySystem() == settings.thisSystem ? 1 :
                        k->second.first.querySystem() == settings.thisSystem ? -1 : 0;
                    if (d == 0)
                        d = comparePriorities(state, j.first, k->second.first);
                    if (d == 0)
                        d = compareVersions(drvName.version, DrvName(k->second.first.queryName()).version);
                }

                if (d > 0) {
                    newest.erase(drvName.name);
                    newest.insert(Newest::value_type(drvName.name, j));
                    multiple.erase(j.first.queryName());
                } else if (d == 0) {
                    multiple.insert(j.first.queryName());
                }
            }

            matches.clear();
            for (auto & j : newest) {
                if (multiple.find(j.second.first.queryName()) != multiple.end())
                    printInfo(
                        "warning: there are multiple derivations named '%1%'; using the first one",
                        j.second.first.queryName());
                matches.push_back(j.second);
            }
        }

        /* Insert only those elements in the final list that we
           haven't inserted before. */
        for (auto & j : matches)
            if (done.find(j.second) == done.end()) {
                done.insert(j.second);
                elems.push_back(j.first);
            }
    }

    checkSelectorUse(selectors);

    return elems;
}


static bool isPath(const string & s)
{
    return s.find('/') != string::npos;
}


static void queryInstSources(EvalState & state,
    InstallSourceInfo & instSource, const Strings & args,
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
                instSource.systemFilter, *instSource.autoArgs, "", allElems);

            elems = filterBySelector(state, allElems, args, newestOnly);

            break;
        }

        /* Get the available user environment elements from the Nix
           expressions specified on the command line; these should be
           functions that take the default Nix expression file as
           argument, e.g., if the file is `./foo.nix', then the
           argument `x: x.bar' is equivalent to `(x: x.bar)
           (import ./foo.nix)' = `(import ./foo.nix).bar'. */
        case srcNixExprs: {

            Value vArg;
            loadSourceExpr(state, instSource.nixExprPath, vArg);

            for (auto & i : args) {
                Expr * eFun = state.parseExprFromString(i, absPath("."));
                Value vFun, vTmp;
                state.eval(eFun, vFun);
                mkApp(vTmp, vFun, vArg);
                getDerivations(state, vTmp, "", *instSource.autoArgs, elems, true);
            }

            break;
        }

        /* The available user environment elements are specified as a
           list of store paths (which may or may not be
           derivations). */
        case srcStorePaths: {

            for (auto & i : args) {
                Path path = state.store->followLinksToStorePath(i);

                string name = baseNameOf(path);
                string::size_type dash = name.find('-');
                if (dash != string::npos)
                    name = string(name, dash + 1);

                DrvInfo elem(state, "", nullptr);
                elem.setName(name);

                if (isDerivation(path)) {
                    elem.setDrvPath(path);
                    elem.setOutPath(state.store->derivationFromPath(path).findOutput("out"));
                    if (name.size() >= drvExtension.size() &&
                        string(name, name.size() - drvExtension.size()) == drvExtension)
                        name = string(name, 0, name.size() - drvExtension.size());
                }
                else elem.setOutPath(path);

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
                args, newestOnly);
            break;
        }

        case srcAttrPath: {
            Value vRoot;
            loadSourceExpr(state, instSource.nixExprPath, vRoot);
            for (auto & i : args) {
                Value & v(*findAlongAttrPath(state, i, *instSource.autoArgs, vRoot));
                getDerivations(state, v, "", *instSource.autoArgs, elems, true);
            }
            break;
        }
    }
}


static void printMissing(EvalState & state, DrvInfos & elems)
{
    PathSet targets;
    for (auto & i : elems) {
        Path drvPath = i.queryDrvPath();
        if (drvPath != "")
            targets.insert(drvPath);
        else
            targets.insert(i.queryOutPath());
    }

    printMissing(state.store, targets);
}


static bool keep(DrvInfo & drv)
{
    return drv.queryMetaBool("keep", false);
}


static void installDerivations(Globals & globals,
    const Strings & args, const Path & profile)
{
    debug(format("installing derivations"));

    /* Get the set of user environment elements to be installed. */
    DrvInfos newElems, newElemsTmp;
    queryInstSources(*globals.state, globals.instSource, args, newElemsTmp, true);

    /* If --prebuilt-only is given, filter out source-only packages. */
    for (auto & i : newElemsTmp)
        if (!globals.prebuiltOnly || isPrebuilt(*globals.state, i))
            newElems.push_back(i);

    StringSet newNames;
    for (auto & i : newElems) {
        /* `forceName' is a hack to get package names right in some
           one-click installs, namely those where the name used in the
           path is not the one we want (e.g., `java-front' versus
           `java-front-0.9pre15899'). */
        if (globals.forceName != "")
            i.setName(globals.forceName);
        newNames.insert(DrvName(i.queryName()).name);
    }


    while (true) {
        string lockToken = optimisticLockProfile(profile);

        DrvInfos allElems(newElems);

        /* Add in the already installed derivations, unless they have
           the same name as a to-be-installed element. */
        if (!globals.removeAll) {
            DrvInfos installedElems = queryInstalled(*globals.state, profile);

            for (auto & i : installedElems) {
                DrvName drvName(i.queryName());
                if (!globals.preserveInstalled &&
                    newNames.find(drvName.name) != newNames.end() &&
                    !keep(i))
                    printInfo("replacing old '%s'", i.queryName());
                else
                    allElems.push_back(i);
            }

            for (auto & i : newElems)
                printInfo("installing '%s'", i.queryName());
        }

        printMissing(*globals.state, newElems);

        if (globals.dryRun) return;

        if (createUserEnv(*globals.state, allElems,
                profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opInstall(Globals & globals, Strings opFlags, Strings opArgs)
{
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else if (arg == "--preserve-installed" || arg == "-P")
            globals.preserveInstalled = true;
        else if (arg == "--remove-all" || arg == "-r")
            globals.removeAll = true;
        else throw UsageError(format("unknown flag '%1%'") % arg);
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

        DrvInfos installedElems = queryInstalled(*globals.state, globals.profile);

        /* Fetch all derivations from the input file. */
        DrvInfos availElems;
        queryInstSources(*globals.state, globals.instSource, args, availElems, false);

        /* Go through all installed derivations. */
        DrvInfos newElems;
        for (auto & i : installedElems) {
            DrvName drvName(i.queryName());

            try {

                if (keep(i)) {
                    newElems.push_back(i);
                    continue;
                }

                /* Find the derivation in the input Nix expression
                   with the same name that satisfies the version
                   constraints specified by upgradeType.  If there are
                   multiple matches, take the one with the highest
                   priority.  If there are still multiple matches,
                   take the one with the highest version.
                   Do not upgrade if it would decrease the priority. */
                DrvInfos::iterator bestElem = availElems.end();
                string bestVersion;
                for (auto j = availElems.begin(); j != availElems.end(); ++j) {
                    if (comparePriorities(*globals.state, i, *j) > 0)
                        continue;
                    DrvName newName(j->queryName());
                    if (newName.name == drvName.name) {
                        int d = compareVersions(drvName.version, newName.version);
                        if ((upgradeType == utLt && d < 0) ||
                            (upgradeType == utLeq && d <= 0) ||
                            (upgradeType == utEq && d == 0) ||
                            upgradeType == utAlways)
                        {
                            long d2 = -1;
                            if (bestElem != availElems.end()) {
                                d2 = comparePriorities(*globals.state, *bestElem, *j);
                                if (d2 == 0) d2 = compareVersions(bestVersion, newName.version);
                            }
                            if (d2 < 0 && (!globals.prebuiltOnly || isPrebuilt(*globals.state, *j))) {
                                bestElem = j;
                                bestVersion = newName.version;
                            }
                        }
                    }
                }

                if (bestElem != availElems.end() &&
                    i.queryOutPath() !=
                    bestElem->queryOutPath())
                {
                    const char * action = compareVersions(drvName.version, bestVersion) <= 0
                        ? "upgrading" : "downgrading";
                    printInfo("%1% '%2%' to '%3%'",
                        action, i.queryName(), bestElem->queryName());
                    newElems.push_back(*bestElem);
                } else newElems.push_back(i);

            } catch (Error & e) {
                e.addPrefix(fmt("while trying to find an upgrade for '%s':\n", i.queryName()));
                throw;
            }
        }

        printMissing(*globals.state, newElems);

        if (globals.dryRun) return;

        if (createUserEnv(*globals.state, newElems,
                globals.profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opUpgrade(Globals & globals, Strings opFlags, Strings opArgs)
{
    UpgradeType upgradeType = utLt;
    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else if (arg == "--lt") upgradeType = utLt;
        else if (arg == "--leq") upgradeType = utLeq;
        else if (arg == "--eq") upgradeType = utEq;
        else if (arg == "--always") upgradeType = utAlways;
        else throw UsageError(format("unknown flag '%1%'") % arg);
    }

    upgradeDerivations(globals, opArgs, upgradeType);
}


static void setMetaFlag(EvalState & state, DrvInfo & drv,
    const string & name, const string & value)
{
    Value * v = state.allocValue();
    mkString(*v, value.c_str());
    drv.setMeta(name, v);
}


static void opSetFlag(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());
    if (opArgs.size() < 2)
        throw UsageError("not enough arguments to '--set-flag'");

    Strings::iterator arg = opArgs.begin();
    string flagName = *arg++;
    string flagValue = *arg++;
    DrvNames selectors = drvNamesFromArgs(Strings(arg, opArgs.end()));

    while (true) {
        string lockToken = optimisticLockProfile(globals.profile);

        DrvInfos installedElems = queryInstalled(*globals.state, globals.profile);

        /* Update all matching derivations. */
        for (auto & i : installedElems) {
            DrvName drvName(i.queryName());
            for (auto & j : selectors)
                if (j.matches(drvName)) {
                    printInfo("setting flag on '%1%'", i.queryName());
                    j.hits++;
                    setMetaFlag(*globals.state, i, flagName, flagValue);
                    break;
                }
        }

        checkSelectorUse(selectors);

        /* Write the new user environment. */
        if (createUserEnv(*globals.state, installedElems,
                globals.profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opSet(Globals & globals, Strings opFlags, Strings opArgs)
{
    auto store2 = globals.state->store.dynamic_pointer_cast<LocalFSStore>();
    if (!store2) throw Error("--set is not supported for this Nix store");

    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
        string arg = *i++;
        if (parseInstallSourceOptions(globals, i, opFlags, arg)) ;
        else throw UsageError(format("unknown flag '%1%'") % arg);
    }

    DrvInfos elems;
    queryInstSources(*globals.state, globals.instSource, opArgs, elems, true);

    if (elems.size() != 1)
        throw Error("--set requires exactly one derivation");

    DrvInfo & drv(elems.front());

    if (globals.forceName != "")
        drv.setName(globals.forceName);

    if (drv.queryDrvPath() != "") {
        PathSet paths = {drv.queryDrvPath()};
        printMissing(globals.state->store, paths);
        if (globals.dryRun) return;
        globals.state->store->buildPaths(paths, globals.state->repair ? bmRepair : bmNormal);
    }
    else {
        printMissing(globals.state->store, {drv.queryOutPath()});
        if (globals.dryRun) return;
        globals.state->store->ensurePath(drv.queryOutPath());
    }

    debug(format("switching to new user environment"));
    Path generation = createGeneration(ref<LocalFSStore>(store2), globals.profile, drv.queryOutPath());
    switchLink(globals.profile, generation);
}


static void uninstallDerivations(Globals & globals, Strings & selectors,
    Path & profile)
{
    while (true) {
        string lockToken = optimisticLockProfile(profile);

        DrvInfos installedElems = queryInstalled(*globals.state, profile);
        DrvInfos newElems;

        for (auto & i : installedElems) {
            DrvName drvName(i.queryName());
            bool found = false;
            for (auto & j : selectors)
                /* !!! the repeated calls to followLinksToStorePath()
                   are expensive, should pre-compute them. */
                if ((isPath(j) && i.queryOutPath() == globals.state->store->followLinksToStorePath(j))
                    || DrvName(j).matches(drvName))
                {
                    printInfo("uninstalling '%s'", i.queryName());
                    found = true;
                    break;
                }
            if (!found) newElems.push_back(i);
        }

        if (globals.dryRun) return;

        if (createUserEnv(*globals.state, newElems,
                profile, settings.envKeepDerivations, lockToken)) break;
    }
}


static void opUninstall(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());
    uninstallDerivations(globals, opArgs, globals.profile);
}


static bool cmpChars(char a, char b)
{
    return toupper(a) < toupper(b);
}


static bool cmpElemByName(const DrvInfo & a, const DrvInfo & b)
{
    auto a_name = a.queryName();
    auto b_name = b.queryName();
    return lexicographical_compare(
        a_name.begin(), a_name.end(),
        b_name.begin(), b_name.end(), cmpChars);
}


typedef list<Strings> Table;


void printTable(Table & table)
{
    auto nrColumns = table.size() > 0 ? table.front().size() : 0;

    vector<size_t> widths;
    widths.resize(nrColumns);

    for (auto & i : table) {
        assert(i.size() == nrColumns);
        Strings::iterator j;
        size_t column;
        for (j = i.begin(), column = 0; j != i.end(); ++j, ++column)
            if (j->size() > widths[column]) widths[column] = j->size();
    }

    for (auto & i : table) {
        Strings::iterator j;
        size_t column;
        for (j = i.begin(), column = 0; j != i.end(); ++j, ++column) {
            string s = *j;
            replace(s.begin(), s.end(), '\n', ' ');
            cout << s;
            if (column < nrColumns - 1)
                cout << string(widths[column] - s.size() + 2, ' ');
        }
        cout << std::endl;
    }
}


/* This function compares the version of an element against the
   versions in the given set of elements.  `cvLess' means that only
   lower versions are in the set, `cvEqual' means that at most an
   equal version is in the set, and `cvGreater' means that there is at
   least one element with a higher version in the set.  `cvUnavail'
   means that there are no elements with the same name in the set. */

typedef enum { cvLess, cvEqual, cvGreater, cvUnavail } VersionDiff;

static VersionDiff compareVersionAgainstSet(
    const DrvInfo & elem, const DrvInfos & elems, string & version)
{
    DrvName name(elem.queryName());

    VersionDiff diff = cvUnavail;
    version = "?";

    for (auto & i : elems) {
        DrvName name2(i.queryName());
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


static void queryJSON(Globals & globals, vector<DrvInfo> & elems)
{
    JSONObject topObj(cout, true);
    for (auto & i : elems) {
        JSONObject pkgObj = topObj.object(i.attrPath);

        auto drvName = DrvName(i.queryName());
        pkgObj.attr("name", drvName.fullName);
        pkgObj.attr("pname", drvName.name);
        pkgObj.attr("version", drvName.version);
        pkgObj.attr("system", i.querySystem());

        JSONObject metaObj = pkgObj.object("meta");
        StringSet metaNames = i.queryMetaNames();
        for (auto & j : metaNames) {
            auto placeholder = metaObj.placeholder(j);
            Value * v = i.queryMeta(j);
            if (!v) {
                printError("derivation '%s' has invalid meta attribute '%s'", i.queryName(), j);
                placeholder.write(nullptr);
            } else {
                PathSet context;
                printValueAsJSON(*globals.state, true, *v, placeholder, context);
            }
        }
    }
}


static void opQuery(Globals & globals, Strings opFlags, Strings opArgs)
{
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
    bool compareVersions = false;
    bool xmlOutput = false;
    bool jsonOutput = false;

    enum { sInstalled, sAvailable } source = sInstalled;

    settings.readOnlyMode = true; /* makes evaluation a bit faster */

    for (Strings::iterator i = opFlags.begin(); i != opFlags.end(); ) {
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
        else if (arg == "--xml") xmlOutput = true;
        else if (arg == "--json") jsonOutput = true;
        else if (arg == "--attr-path" || arg == "-P") printAttrPath = true;
        else if (arg == "--attr" || arg == "-A")
            attrPath = needArg(i, opFlags, arg);
        else
            throw UsageError(format("unknown flag '%1%'") % arg);
    }


    /* Obtain derivation information from the specified source. */
    DrvInfos availElems, installedElems;

    if (source == sInstalled || compareVersions || printStatus)
        installedElems = queryInstalled(*globals.state, globals.profile);

    if (source == sAvailable || compareVersions)
        loadDerivations(*globals.state, globals.instSource.nixExprPath,
            globals.instSource.systemFilter, *globals.instSource.autoArgs,
            attrPath, availElems);

    DrvInfos elems_ = filterBySelector(*globals.state,
        source == sInstalled ? installedElems : availElems,
        opArgs, false);

    DrvInfos & otherElems(source == sInstalled ? availElems : installedElems);


    /* Sort them by name. */
    /* !!! */
    vector<DrvInfo> elems;
    for (auto & i : elems_) elems.push_back(i);
    sort(elems.begin(), elems.end(), cmpElemByName);


    /* We only need to know the installed paths when we are querying
       the status of the derivation. */
    PathSet installed; /* installed paths */

    if (printStatus) {
        for (auto & i : installedElems)
            installed.insert(i.queryOutPath());
    }


    /* Query which paths have substitutes. */
    PathSet validPaths, substitutablePaths;
    if (printStatus || globals.prebuiltOnly) {
        PathSet paths;
        for (auto & i : elems)
            try {
                paths.insert(i.queryOutPath());
            } catch (AssertionError & e) {
                printMsg(lvlTalkative, "skipping derivation named '%s' which gives an assertion failure", i.queryName());
                i.setFailed();
            }
        validPaths = globals.state->store->queryValidPaths(paths);
        substitutablePaths = globals.state->store->querySubstitutablePaths(paths);
    }


    /* Print the desired columns, or XML output. */
    if (jsonOutput) {
        queryJSON(globals, elems);
        return;
    }

    bool tty = isatty(STDOUT_FILENO);
    RunPager pager;

    Table table;
    std::ostringstream dummy;
    XMLWriter xml(true, *(xmlOutput ? &cout : &dummy));
    XMLOpenElement xmlRoot(xml, "items");

    for (auto & i : elems) {
        try {
            if (i.hasFailed()) continue;

            //Activity act(*logger, lvlDebug, format("outputting query result '%1%'") % i.attrPath);

            if (globals.prebuiltOnly &&
                validPaths.find(i.queryOutPath()) == validPaths.end() &&
                substitutablePaths.find(i.queryOutPath()) == substitutablePaths.end())
                continue;

            /* For table output. */
            Strings columns;

            /* For XML output. */
            XMLAttrs attrs;

            if (printStatus) {
                Path outPath = i.queryOutPath();
                bool hasSubs = substitutablePaths.find(outPath) != substitutablePaths.end();
                bool isInstalled = installed.find(outPath) != installed.end();
                bool isValid = validPaths.find(outPath) != validPaths.end();
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
                attrs["attrPath"] = i.attrPath;
            else if (printAttrPath)
                columns.push_back(i.attrPath);

            if (xmlOutput) {
                auto drvName = DrvName(i.queryName());
                attrs["name"] = drvName.fullName;
                attrs["pname"] = drvName.name;
                attrs["version"] = drvName.version;
            } else if (printName) {
                columns.push_back(i.queryName());
            }

            if (compareVersions) {
                /* Compare this element against the versions of the
                   same named packages in either the set of available
                   elements, or the set of installed elements.  !!!
                   This is O(N * M), should be O(N * lg M). */
                string version;
                VersionDiff diff = compareVersionAgainstSet(i, otherElems, version);

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
                    if (diff == cvGreater && tty)
                        column = ANSI_RED + column + ANSI_NORMAL;
                    columns.push_back(column);
                }
            }

            if (xmlOutput) {
                if (i.querySystem() != "") attrs["system"] = i.querySystem();
            }
            else if (printSystem)
                columns.push_back(i.querySystem());

            if (printDrvPath) {
                string drvPath = i.queryDrvPath();
                if (xmlOutput) {
                    if (drvPath != "") attrs["drvPath"] = drvPath;
                } else
                    columns.push_back(drvPath == "" ? "-" : drvPath);
            }

            if (printOutPath && !xmlOutput) {
                DrvInfo::Outputs outputs = i.queryOutputs();
                string s;
                for (auto & j : outputs) {
                    if (!s.empty()) s += ';';
                    if (j.first != "out") { s += j.first; s += "="; }
                    s += j.second;
                }
                columns.push_back(s);
            }

            if (printDescription) {
                string descr = i.queryMetaString("description");
                if (xmlOutput) {
                    if (descr != "") attrs["description"] = descr;
                } else
                    columns.push_back(descr);
            }

            if (xmlOutput) {
                if (printOutPath || printMeta) {
                    XMLOpenElement item(xml, "item", attrs);
                    if (printOutPath) {
                        DrvInfo::Outputs outputs = i.queryOutputs();
                        for (auto & j : outputs) {
                            XMLAttrs attrs2;
                            attrs2["name"] = j.first;
                            attrs2["path"] = j.second;
                            xml.writeEmptyElement("output", attrs2);
                        }
                    }
                    if (printMeta) {
                        StringSet metaNames = i.queryMetaNames();
                        for (auto & j : metaNames) {
                            XMLAttrs attrs2;
                            attrs2["name"] = j;
                            Value * v = i.queryMeta(j);
                            if (!v)
                                printError("derivation '%s' has invalid meta attribute '%s'", i.queryName(), j);
                            else {
                                if (v->type == tString) {
                                    attrs2["type"] = "string";
                                    attrs2["value"] = v->string.s;
                                    xml.writeEmptyElement("meta", attrs2);
                                } else if (v->type == tInt) {
                                    attrs2["type"] = "int";
                                    attrs2["value"] = (format("%1%") % v->integer).str();
                                    xml.writeEmptyElement("meta", attrs2);
                                } else if (v->type == tFloat) {
                                    attrs2["type"] = "float";
                                    attrs2["value"] = (format("%1%") % v->fpoint).str();
                                    xml.writeEmptyElement("meta", attrs2);
                                } else if (v->type == tBool) {
                                    attrs2["type"] = "bool";
                                    attrs2["value"] = v->boolean ? "true" : "false";
                                    xml.writeEmptyElement("meta", attrs2);
                                } else if (v->isList()) {
                                    attrs2["type"] = "strings";
                                    XMLOpenElement m(xml, "meta", attrs2);
                                    for (unsigned int j = 0; j < v->listSize(); ++j) {
                                        if (v->listElems()[j]->type != tString) continue;
                                        XMLAttrs attrs3;
                                        attrs3["value"] = v->listElems()[j]->string.s;
                                        xml.writeEmptyElement("string", attrs3);
                                    }
                              } else if (v->type == tAttrs) {
                                  attrs2["type"] = "strings";
                                  XMLOpenElement m(xml, "meta", attrs2);
                                  Bindings & attrs = *v->attrs;
                                  for (auto &i : attrs) {
                                      Attr & a(*attrs.find(i.name));
                                      if(a.value->type != tString) continue;
                                      XMLAttrs attrs3;
                                      attrs3["type"] = i.name;
                                      attrs3["value"] = a.value->string.s;
                                      xml.writeEmptyElement("string", attrs3);
                                }
                              }
                            }
                        }
                    }
                } else
                    xml.writeEmptyElement("item", attrs);
            } else
                table.push_back(columns);

            cout.flush();

        } catch (AssertionError & e) {
            printMsg(lvlTalkative, "skipping derivation named '%1%' which gives an assertion failure", i.queryName());
        } catch (Error & e) {
            e.addPrefix(fmt("while querying the derivation named '%1%':\n", i.queryName()));
            throw;
        }
    }

    if (!xmlOutput) printTable(table);
}


static void opSwitchProfile(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("exactly one argument expected"));

    Path profile = absPath(opArgs.front());
    Path profileLink = getHome() + "/.nix-profile";

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
    for (auto & i : gens)
        if ((dstGen == prevGen && i.number < curGen) ||
            (dstGen >= 0 && i.number == dstGen))
            dst = i;

    if (!dst) {
        if (dstGen == prevGen)
            throw Error(format("no generation older than the current (%1%) exists")
                % curGen);
        else
            throw Error(format("generation %1% does not exist") % dstGen);
    }

    printInfo(format("switching from generation %1% to %2%")
        % curGen % dst.number);

    if (globals.dryRun) return;

    switchLink(globals.profile, dst.path);
}


static void opSwitchGeneration(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());
    if (opArgs.size() != 1)
        throw UsageError(format("exactly one argument expected"));

    int dstGen;
    if (!string2Int(opArgs.front(), dstGen))
        throw UsageError(format("expected a generation number"));

    switchGeneration(globals, dstGen);
}


static void opRollback(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError(format("no arguments expected"));

    switchGeneration(globals, prevGen);
}


static void opListGenerations(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());
    if (opArgs.size() != 0)
        throw UsageError(format("no arguments expected"));

    PathLocks lock;
    lockProfile(lock, globals.profile);

    int curGen;
    Generations gens = findGenerations(globals.profile, curGen);

    RunPager pager;

    for (auto & i : gens) {
        tm t;
        if (!localtime_r(&i.creationTime, &t)) throw Error("cannot convert time");
        cout << format("%|4|   %|4|-%|02|-%|02| %|02|:%|02|:%|02|   %||\n")
            % i.number
            % (t.tm_year + 1900) % (t.tm_mon + 1) % t.tm_mday
            % t.tm_hour % t.tm_min % t.tm_sec
            % (i.number == curGen ? "(current)" : "");
    }
}


static void opDeleteGenerations(Globals & globals, Strings opFlags, Strings opArgs)
{
    if (opFlags.size() > 0)
        throw UsageError(format("unknown flag '%1%'") % opFlags.front());

    if (opArgs.size() == 1 && opArgs.front() == "old") {
        deleteOldGenerations(globals.profile, globals.dryRun);
    } else if (opArgs.size() == 1 && opArgs.front().find('d') != string::npos) {
        deleteGenerationsOlderThan(globals.profile, opArgs.front(), globals.dryRun);
    } else if (opArgs.size() == 1 && opArgs.front().find('+') != string::npos) {
        if(opArgs.front().size() < 2)
            throw Error(format("invalid number of generations ‘%1%’") % opArgs.front());
        string str_max = string(opArgs.front(), 1, opArgs.front().size());
        int max;
        if (!string2Int(str_max, max) || max == 0)
            throw Error(format("invalid number of generations to keep ‘%1%’") % opArgs.front());
        deleteGenerationsGreaterThan(globals.profile, max, globals.dryRun);
    } else {
        std::set<unsigned int> gens;
        for (auto & i : opArgs) {
            unsigned int n;
            if (!string2Int(i, n))
                throw UsageError(format("invalid generation number '%1%'") % i);
            gens.insert(n);
        }
        deleteGenerations(globals.profile, gens, globals.dryRun);
    }
}


static void opVersion(Globals & globals, Strings opFlags, Strings opArgs)
{
    printVersion("nix-env");
}


static int _main(int argc, char * * argv)
{
    {
        Strings opFlags, opArgs;
        Operation op = 0;
        RepairFlag repair = NoRepair;
        string file;

        Globals globals;

        globals.instSource.type = srcUnknown;
        globals.instSource.nixExprPath = getHome() + "/.nix-defexpr";
        globals.instSource.systemFilter = "*";

        if (!pathExists(globals.instSource.nixExprPath)) {
            try {
                createDirs(globals.instSource.nixExprPath);
                replaceSymlink(
                    fmt("%s/profiles/per-user/%s/channels", settings.nixStateDir, getUserName()),
                    globals.instSource.nixExprPath + "/channels");
                if (getuid() != 0)
                    replaceSymlink(
                        fmt("%s/profiles/per-user/root/channels", settings.nixStateDir),
                        globals.instSource.nixExprPath + "/channels_root");
            } catch (Error &) { }
        }

        globals.dryRun = false;
        globals.preserveInstalled = false;
        globals.removeAll = false;
        globals.prebuiltOnly = false;

        struct MyArgs : LegacyArgs, MixEvalArgs
        {
            using LegacyArgs::LegacyArgs;
        };

        MyArgs myArgs(baseNameOf(argv[0]), [&](Strings::iterator & arg, const Strings::iterator & end) {
            Operation oldOp = op;

            if (*arg == "--help")
                showManPage("nix-env");
            else if (*arg == "--version")
                op = opVersion;
            else if (*arg == "--install" || *arg == "-i")
                op = opInstall;
            else if (*arg == "--force-name") // undocumented flag for nix-install-package
                globals.forceName = getArg(*arg, arg, end);
            else if (*arg == "--uninstall" || *arg == "-e")
                op = opUninstall;
            else if (*arg == "--upgrade" || *arg == "-u")
                op = opUpgrade;
            else if (*arg == "--set-flag")
                op = opSetFlag;
            else if (*arg == "--set")
                op = opSet;
            else if (*arg == "--query" || *arg == "-q")
                op = opQuery;
            else if (*arg == "--profile" || *arg == "-p")
                globals.profile = absPath(getArg(*arg, arg, end));
            else if (*arg == "--file" || *arg == "-f")
                file = getArg(*arg, arg, end);
            else if (*arg == "--switch-profile" || *arg == "-S")
                op = opSwitchProfile;
            else if (*arg == "--switch-generation" || *arg == "-G")
                op = opSwitchGeneration;
            else if (*arg == "--rollback")
                op = opRollback;
            else if (*arg == "--list-generations")
                op = opListGenerations;
            else if (*arg == "--delete-generations")
                op = opDeleteGenerations;
            else if (*arg == "--dry-run") {
                printInfo("(dry run; not doing anything)");
                globals.dryRun = true;
            }
            else if (*arg == "--system-filter")
                globals.instSource.systemFilter = getArg(*arg, arg, end);
            else if (*arg == "--prebuilt-only" || *arg == "-b")
                globals.prebuiltOnly = true;
            else if (*arg == "--repair")
                repair = Repair;
            else if (*arg != "" && arg->at(0) == '-') {
                opFlags.push_back(*arg);
                /* FIXME: hacky */
                if (*arg == "--from-profile" ||
                    (op == opQuery && (*arg == "--attr" || *arg == "-A")))
                    opFlags.push_back(getArg(*arg, arg, end));
            }
            else
                opArgs.push_back(*arg);

            if (oldOp && oldOp != op)
                throw UsageError("only one operation may be specified");

            return true;
        });

        myArgs.parseCmdline(argvToStrings(argc, argv));

        initPlugins();

        if (!op) throw UsageError("no operation specified");

        auto store = openStore();

        globals.state = std::shared_ptr<EvalState>(new EvalState(myArgs.searchPath, store));
        globals.state->repair = repair;

        if (file != "")
            globals.instSource.nixExprPath = lookupFileArg(*globals.state, file);

        globals.instSource.autoArgs = myArgs.getAutoArgs(*globals.state);

        if (globals.profile == "")
            globals.profile = getEnv("NIX_PROFILE", "");

        if (globals.profile == "") {
            Path profileLink = getHome() + "/.nix-profile";
            try {
                if (!pathExists(profileLink)) {
                    replaceSymlink(
                        getuid() == 0
                        ? settings.nixStateDir + "/profiles/default"
                        : fmt("%s/profiles/per-user/%s/profile", settings.nixStateDir, getUserName()),
                        profileLink);
                }
                globals.profile = absPath(readLink(profileLink), dirOf(profileLink));
            } catch (Error &) {
                globals.profile = profileLink;
            }
        }

        op(globals, opFlags, opArgs);

        globals.state->printStats();

        return 0;
    }
}

static RegisterLegacyCommand s1("nix-env", _main);
