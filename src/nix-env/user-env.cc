#include "util.hh"
#include "get-drvs.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "shared.hh"
#include "eval.hh"
#include "parser.hh"
#include "profiles.hh"


namespace nix {


static void readLegacyManifest(const Path & path, DrvInfos & elems);


DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;

    Path manifestFile = userEnv + "/manifest.nix";
    Path oldManifestFile = userEnv + "/manifest";

    if (pathExists(manifestFile)) {
        Value v;
        state.eval(parseExprFromFile(state, manifestFile), v);
        Bindings bindings;
        getDerivations(state, v, "", bindings, elems);
    } else if (pathExists(oldManifestFile))
        readLegacyManifest(oldManifestFile, elems);

    return elems;
}


bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const string & lockToken)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    PathSet drvsToBuild;
    foreach (DrvInfos::const_iterator, i, elems)
        if (i->queryDrvPath(state) != "")
            drvsToBuild.insert(i->queryDrvPath(state));

    debug(format("building user environment dependencies"));
    store->buildDerivations(drvsToBuild);

    /* Construct the whole top level derivation. */
    PathSet references;
    Value manifest;
    state.mkList(manifest, elems.size());
    unsigned int n = 0;
    foreach (DrvInfos::iterator, i, elems) {
        /* Create a pseudo-derivation containing the name, system,
           output path, and optionally the derivation path, as well as
           the meta attributes. */
        Path drvPath = keepDerivations ? i->queryDrvPath(state) : "";

        Value & v(*state.allocValue());
        manifest.list.elems[n++] = &v;
        state.mkAttrs(v, 8);

        mkString(*state.allocAttr(v, state.sType), "derivation");
        mkString(*state.allocAttr(v, state.sName), i->name);
        mkString(*state.allocAttr(v, state.sSystem), i->system);
        mkString(*state.allocAttr(v, state.sOutPath), i->queryOutPath(state));
        if (drvPath != "")
            mkString(*state.allocAttr(v, state.sDrvPath), i->queryDrvPath(state));

        Value & vMeta = *state.allocAttr(v, state.sMeta);
        state.mkAttrs(vMeta, 16);

        MetaInfo meta = i->queryMetaInfo(state);

        foreach (MetaInfo::const_iterator, j, meta) {
            Value & v2(*state.allocAttr(vMeta, state.symbols.create(j->first)));
            switch (j->second.type) {
                case MetaValue::tpInt: mkInt(v2, j->second.intValue); break;
                case MetaValue::tpString: mkString(v2, j->second.stringValue); break;
                case MetaValue::tpStrings: {
                    state.mkList(v2, j->second.stringValues.size());
                    unsigned int m = 0;
                    foreach (Strings::const_iterator, k, j->second.stringValues) {
                        v2.list.elems[m] = state.allocValue();
                        mkString(*v2.list.elems[m++], *k);
                    }
                    break;
                }
                default: abort();
            }
        }
    
        vMeta.attrs->sort();
        v.attrs->sort();
        
        /* This is only necessary when installing store paths, e.g.,
           `nix-env -i /nix/store/abcd...-foo'. */
        store->addTempRoot(i->queryOutPath(state));
        store->ensurePath(i->queryOutPath(state));
        
        references.insert(i->queryOutPath(state));
        if (drvPath != "") references.insert(drvPath);
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    Path manifestFile = store->addTextToStore("env-manifest.nix",
        (format("%1%") % manifest).str(), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.eval(parseExprFromFile(state, nixDataDir + "/nix/corepkgs/buildenv"), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    Value args, topLevel;
    state.mkAttrs(args, 3);
    mkString(*state.allocAttr(args, state.sSystem), thisSystem);
    mkString(*state.allocAttr(args, state.symbols.create("manifest")),
        manifestFile, singleton<PathSet>(manifestFile));
    args.attrs->push_back(Attr(state.symbols.create("derivations"), &manifest));
    args.attrs->sort();
    mkApp(topLevel, envBuilder, args);
        
    /* Evaluate it. */
    debug("evaluating user environment builder");
    DrvInfo topLevelDrv;
    if (!getDerivation(state, topLevel, topLevelDrv))
        abort();
    
    /* Realise the resulting store expression. */
    debug("building user environment");
    store->buildDerivations(singleton<PathSet>(topLevelDrv.queryDrvPath(state)));

    /* Switch the current user environment to the output path. */
    PathLocks lock;
    lockProfile(lock, profile);

    Path lockTokenCur = optimisticLockProfile(profile);
    if (lockToken != lockTokenCur) {
        printMsg(lvlError, format("profile `%1%' changed while we were busy; restarting") % profile);
        return false;
    }
    
    debug(format("switching to new user environment"));
    Path generation = createGeneration(profile, topLevelDrv.queryOutPath(state));
    switchLink(profile, generation);

    return true;
}


/* Code for parsing manifests in the old textual ATerm format. */

static string parseStr(std::istream & str)
{
    expect(str, "Str(");
    string s = parseString(str);
    expect(str, ",[])");
    return s;
}


static string parseWord(std::istream & str)
{
    string res;
    while (isalpha(str.peek()))
        res += str.get();
    return res;
}


static MetaInfo parseMeta(std::istream & str)
{
    MetaInfo meta;

    expect(str, "Attrs([");
    while (!endOfList(str)) {
        expect(str, "Bind(");

        MetaValue value;
        
        string name = parseString(str);
        expect(str, ",");

        string type = parseWord(str);

        if (type == "Str") {
            expect(str, "(");
            value.type = MetaValue::tpString;
            value.stringValue = parseString(str);
            expect(str, ",[])");
        }

        else if (type == "List") {
            expect(str, "([");
            value.type = MetaValue::tpStrings;
            while (!endOfList(str))
                value.stringValues.push_back(parseStr(str));
            expect(str, ")");
        }

        else throw Error(format("unexpected token `%1%'") % type);

        expect(str, ",NoPos)");
        meta[name] = value;
    }
    
    expect(str, ")");

    return meta;
}


static void readLegacyManifest(const Path & path, DrvInfos & elems)
{
    string manifest = readFile(path);
    std::istringstream str(manifest);
    expect(str, "List([");

    unsigned int n = 0;
    
    while (!endOfList(str)) {
        DrvInfo elem;
        expect(str, "Attrs([");

        while (!endOfList(str)) {
            expect(str, "Bind(");
            string name = parseString(str);
            expect(str, ",");
            
            if (name == "meta") elem.setMetaInfo(parseMeta(str));
            else {
                string value = parseStr(str);
                if (name == "name") elem.name = value;
                else if (name == "outPath") elem.setOutPath(value);
                else if (name == "drvPath") elem.setDrvPath(value);
                else if (name == "system") elem.system = value;
            }

            expect(str, ",NoPos)");
        }

        expect(str, ")");

        if (elem.name != "") {
            elem.attrPath = int2String(n++);
            elems.push_back(elem);
        }
    }

    expect(str, ")");
}


}

