#include "fstate.hh"
#include "globals.hh"
#include "store.hh"


string printTerm(ATerm t)
{
    char * s = ATwriteToString(t);
    return s;
}


Error badTerm(const format & f, ATerm t)
{
    return Error(format("%1%, in `%2%'") % f.str() % printTerm(t));
}


Hash hashTerm(ATerm t)
{
    return hashString(printTerm(t));
}


ATerm termFromId(const FSId & id)
{
    string path = expandId(id);
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % path);
    return t;
}


FSId writeTerm(ATerm t, const string & suffix, FSId id)
{
    /* By default, the id of a term is its hash. */
    if (id == FSId()) id = hashTerm(t);

    string path = canonPath(nixStore + "/" + 
        (string) id + suffix + ".nix");
    if (!ATwriteToNamedTextFile(t, path.c_str()))
        throw Error(format("cannot write aterm %1%") % path);

//     debug(format("written term %1% = %2%") % (string) id %
//         printTerm(t));

    Transaction txn(nixDB);
    registerPath(txn, path, id);
    txn.commit();

    return id;
}


static void parsePaths(ATermList paths, StringSet & out)
{
    while (!ATisEmpty(paths)) {
        char * s;
        ATerm t = ATgetFirst(paths);
        if (!ATmatch(t, "<str>", &s))
            throw badTerm("not a path", t);
        out.insert(s);
        paths = ATgetNext(paths);
    }
}


static void checkSlice(const Slice & slice)
{
    if (slice.elems.size() == 0)
        throw Error("empty slice");

    StringSet decl;
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
        decl.insert(i->first);
    
    for (StringSet::const_iterator i = slice.roots.begin();
         i != slice.roots.end(); i++)
        if (decl.find(*i) == decl.end())
            throw Error(format("undefined root path `%1%'") % *i);
    
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
        for (StringSet::const_iterator j = i->second.refs.begin();
             j != i->second.refs.end(); j++)
            if (decl.find(*j) == decl.end())
                throw Error(
		    format("undefined path `%1%' referenced by `%2%'")
		    % *j % i->first);
}


/* Parse a slice. */
static bool parseSlice(ATerm t, Slice & slice)
{
    ATermList roots, elems;
    
    if (!ATmatch(t, "Slice([<list>], [<list>])", &roots, &elems))
        return false;

    parsePaths(roots, slice.roots);

    while (!ATisEmpty(elems)) {
        char * s1, * s2;
        ATermList refs;
        ATerm t = ATgetFirst(elems);
        if (!ATmatch(t, "(<str>, <str>, [<list>])", &s1, &s2, &refs))
            throw badTerm("not a slice element", t);
        SliceElem elem;
        elem.id = parseHash(s2);
        parsePaths(refs, elem.refs);
        slice.elems[s1] = elem;
        elems = ATgetNext(elems);
    }

    checkSlice(slice);
    return true;
}


static bool parseDerive(ATerm t, Derive & derive)
{
    ATermList outs, ins, args, bnds;
    char * builder;
    char * platform;

    if (!ATmatch(t, "Derive([<list>], [<list>], <str>, <str>, [<list>], [<list>])",
            &outs, &ins, &platform, &builder, &args, &bnds))
    {
        /* !!! compatibility -> remove eventually */
        if (!ATmatch(t, "Derive([<list>], [<list>], <str>, <str>, [<list>])",
                &outs, &ins, &builder, &platform, &bnds))
            return false;
        args = ATempty;
    }

    while (!ATisEmpty(outs)) {
        char * s1, * s2;
        ATerm t = ATgetFirst(outs);
        if (!ATmatch(t, "(<str>, <str>)", &s1, &s2))
            throw badTerm("not a derive output", t);
        derive.outputs[s1] = parseHash(s2);
        outs = ATgetNext(outs);
    }

    while (!ATisEmpty(ins)) {
        char * s;
        ATerm t = ATgetFirst(ins);
        if (!ATmatch(t, "<str>", &s))
            throw badTerm("not an id", t);
        derive.inputs.insert(parseHash(s));
        ins = ATgetNext(ins);
    }

    derive.builder = builder;
    derive.platform = platform;
    
    while (!ATisEmpty(args)) {
        char * s;
        ATerm arg = ATgetFirst(args);
        if (!ATmatch(arg, "<str>", &s))
            throw badTerm("string expected", arg);
        derive.args.push_back(s);
        args = ATgetNext(args);
    }

    while (!ATisEmpty(bnds)) {
        char * s1, * s2;
        ATerm bnd = ATgetFirst(bnds);
        if (!ATmatch(bnd, "(<str>, <str>)", &s1, &s2))
            throw badTerm("tuple of strings expected", bnd);
        derive.env[s1] = s2;
        bnds = ATgetNext(bnds);
    }

    return true;
}


FState parseFState(ATerm t)
{
    FState fs;
    if (parseSlice(t, fs.slice))
        fs.type = FState::fsSlice;
    else if (parseDerive(t, fs.derive))
        fs.type = FState::fsDerive;
    else throw badTerm("not an fstate-expression", t);
    return fs;
}


static ATermList unparsePaths(const StringSet & paths)
{
    ATermList l = ATempty;
    for (StringSet::const_iterator i = paths.begin();
         i != paths.end(); i++)
        l = ATinsert(l, ATmake("<str>", i->c_str()));
    return ATreverse(l);
}


static ATerm unparseSlice(const Slice & slice)
{
    ATermList roots = unparsePaths(slice.roots);
    
    ATermList elems = ATempty;
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
        elems = ATinsert(elems,
            ATmake("(<str>, <str>, <term>)",
                i->first.c_str(),
                ((string) i->second.id).c_str(),
                unparsePaths(i->second.refs)));

    return ATmake("Slice(<term>, <term>)", roots, elems);
}


static ATerm unparseDerive(const Derive & derive)
{
    ATermList outs = ATempty;
    for (DeriveOutputs::const_iterator i = derive.outputs.begin();
         i != derive.outputs.end(); i++)
        outs = ATinsert(outs,
            ATmake("(<str>, <str>)", 
                i->first.c_str(), ((string) i->second).c_str()));
    
    ATermList ins = ATempty;
    for (FSIdSet::const_iterator i = derive.inputs.begin();
         i != derive.inputs.end(); i++)
        ins = ATinsert(ins, ATmake("<str>", ((string) *i).c_str()));

    ATermList args = ATempty;
    for (Strings::const_iterator i = derive.args.begin();
         i != derive.args.end(); i++)
        args = ATinsert(args, ATmake("<str>", i->c_str()));

    ATermList env = ATempty;
    for (StringPairs::const_iterator i = derive.env.begin();
         i != derive.env.end(); i++)
        env = ATinsert(env,
            ATmake("(<str>, <str>)", 
                i->first.c_str(), i->second.c_str()));

    return ATmake("Derive(<term>, <term>, <str>, <str>, <term>, <term>)",
        ATreverse(outs),
        ATreverse(ins),
        derive.platform.c_str(),
        derive.builder.c_str(),
        ATreverse(args),
        ATreverse(env));
}


ATerm unparseFState(const FState & fs)
{
    if (fs.type == FState::fsSlice)
        return unparseSlice(fs.slice);
    else if (fs.type == FState::fsDerive)
        return unparseDerive(fs.derive);
    else abort();
}
