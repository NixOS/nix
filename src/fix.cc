#include <map>
#include <iostream>

#include "globals.hh"
#include "fstate.hh"
#include "store.hh"
#include "shared.hh"


typedef ATerm Expr;


static Expr evalFile(string fileName);


static bool isFState(Expr e, string & path)
{
    char * s1, * s2, * s3;
    Expr e1, e2;
    if (ATmatch(e, "Path(<str>, <term>, [<list>])", &s1, &e1, &e2)) {
        path = s1;
        return true;
    }
    else if (ATmatch(e, "Derive(<str>, <str>, [<list>], <str>, [<list>])",
                   &s1, &s2, &e1, &s3, &e2))
    {
        path = s3;
        return true;
    }
    else if (ATmatch(e, "Include(<str>)", &s1))
    {
        string fn = queryFromStore(parseHash(s1));
        return isFState(evalFile(fn), path);
    }
    else return false;
}


static Expr substExpr(string x, Expr rep, Expr e)
{
    char * s;
    Expr e2;

    if (ATmatch(e, "Var(<str>)", &s))
        if (x == s)
            return rep;
        else
            return e;

    if (ATmatch(e, "Lam(<str>, <term>)", &s, &e2))
        if (x == s)
            return e;
    /* !!! unfair substitutions */

    /* Generically substitute in subterms. */

    if (ATgetType(e) == AT_APPL) {
        AFun fun = ATgetAFun(e);
        int arity = ATgetArity(fun);
        ATermList args = ATempty;

        for (int i = arity - 1; i >= 0; i--)
            args = ATinsert(args, substExpr(x, rep, ATgetArgument(e, i)));
        
        return (ATerm) ATmakeApplList(fun, args);
    }

    if (ATgetType(e) == AT_LIST) {
        ATermList in = (ATermList) e;
        ATermList out = ATempty;

        while (!ATisEmpty(in)) {
            out = ATinsert(out, substExpr(x, rep, ATgetFirst(in)));
            in = ATgetNext(in);
        }

        return (ATerm) ATreverse(out);
    }

    throw badTerm("do not know how to substitute", e);
}


static Expr substExprMany(ATermList formals, ATermList args, Expr body)
{
    char * s;
    Expr e;

    /* !!! check args against formals */

    while (!ATisEmpty(args)) {
        ATerm tup = ATgetFirst(args);
        if (!ATmatch(tup, "(<str>, <term>)", &s, &e))
            throw badTerm("expected an argument tuple", tup);

        body = substExpr(s, e, body);

        args = ATgetNext(args);
    }
    
    return body;
}


static Expr evalExpr(Expr e)
{
    char * s1;
    Expr e1, e2, e3, e4;
    ATermList bnds;

    /* Normal forms. */
    if (ATmatch(e, "<str>", &s1) ||
        ATmatch(e, "Function([<list>], <term>)", &e1, &e2))
        return e;

    string dummy;
    if (isFState(e, dummy)) return e;

    /* Application. */
    if (ATmatch(e, "App(<term>, [<list>])", &e1, &e2)) {
        e1 = evalExpr(e1);
        if (!ATmatch(e1, "Function([<list>], <term>)", &e3, &e4))
            throw badTerm("expecting a function", e1);
        return evalExpr(substExprMany((ATermList) e3, (ATermList) e2, e4));
    }

    /* Fix inclusion. */
    if (ATmatch(e, "IncludeFix(<str>)", &s1)) {
        string fileName(s1);
        return evalFile(s1);
    }

    /* Relative files. */
    if (ATmatch(e, "Relative(<str>)", &s1)) {
        string srcPath = s1;
        string dstPath;
        Hash hash;
        addToStore(srcPath, dstPath, hash);
        return ATmake("Path(<str>, Hash(<str>), [])",
            dstPath.c_str(), ((string) hash).c_str());
    }

    /* Packages are transformed into Derive fstate expressions. */
    if (ATmatch(e, "Package([<list>])", &bnds)) {

        /* Evaluate the bindings and put them in a map. */
        map<string, ATerm> bndMap;
        bndMap["platform"] = ATmake("<str>", SYSTEM);
        while (!ATisEmpty(bnds)) {
            ATerm bnd = ATgetFirst(bnds);
            if (!ATmatch(bnd, "(<str>, <term>)", &s1, &e1))
                throw badTerm("binding expected", bnd);
            bndMap[s1] = evalExpr(e1);
            bnds = ATgetNext(bnds);
        }

        /* Gather information for building the Derive expression. */
        ATermList ins = ATempty, env = ATempty;
        string builder, id;
        bnds = ATempty;

        for (map<string, ATerm>::iterator it = bndMap.begin();
             it != bndMap.end(); it++)
        {
            string key = it->first;
            ATerm value = it->second;

            string path;
            if (isFState(value, path)) {
                ins = ATinsert(ins, value);
                env = ATinsert(env, ATmake("(<str>, <str>)",
                    key.c_str(), path.c_str()));
                if (key == "build") builder = path;
            }
            else if (ATmatch(value, "<str>", &s1)) {
                if (key == "id") id = s1;
                env = ATinsert(env, 
                    ATmake("(<str>, <str>)", key.c_str(), s1));
            }
            else throw badTerm("invalid package argument", value);

            bnds = ATinsert(bnds, 
                ATmake("(<str>, <term>)", key.c_str(), value));
        }

        /* Hash the normal form to produce a unique but deterministic
           path name for this package. */
        ATerm nf = ATmake("Package(<term>)", ATreverse(bnds));
        Hash hash = hashTerm(nf);

        if (builder == "")
            throw badTerm("no builder specified", nf);
        
        if (id == "")
            throw badTerm("no package identifier specified", nf);
        
        string out = nixStore + "/" + ((string) hash).c_str() + "-" + id;

        env = ATinsert(env, ATmake("(<str>, <str>)", "out", out.c_str()));

        /* Construct the result. */
        e = ATmake("Derive(<str>, <str>, <term>, <str>, <term>)",
            SYSTEM, builder.c_str(), ins, out.c_str(), env);

        /* Write the resulting term into the Nix store directory. */
        Hash eHash = writeTerm(e);

        return ATmake("Include(<str>)", ((string) eHash).c_str());
    }

    /* Barf. */
    throw badTerm("invalid expression", e);
}


static Strings searchPath;


static Expr evalFile(string fileName)
{
    Expr e = ATreadFromNamedFile(fileName.c_str());
    if (!e) throw Error(format("cannot read aterm `%1%'") % fileName);
    return evalExpr(e);
}


void run(Strings args)
{
    Strings files;

    searchPath.push_back(".");
    
    for (Strings::iterator it = args.begin();
         it != args.end(); )
    {
        string arg = *it++;

        if (arg == "--includedir" || arg == "-I") {
            if (it == args.end())
                throw UsageError(format("argument required in `%1%'") % arg);
            searchPath.push_back(*it++);
        }
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%`") % arg);
        else
            files.push_back(arg);
    }

    if (files.empty()) throw UsageError("no files specified");

    for (Strings::iterator it = files.begin();
         it != files.end(); it++)
    {
        Expr e = evalFile(*it);
        char * s;
        if (ATmatch(e, "Include(<str>)", &s)) {
            cout << format("%1%\n") % s;
        }
        else throw badTerm("top level is not a package", e);
    }
}


string programId = "fix";
