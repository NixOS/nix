#include <map>
#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "fstate.hh"
#include "globals.hh"
#include "store.hh"
#include "db.hh"
#include "references.hh"


/* A Unix environment is a mapping from strings to strings. */
typedef map<string, string> Environment;


class AutoDelete
{
    string path;
public:

    AutoDelete(const string & p) : path(p) 
    {
    }

    ~AutoDelete()
    {
        deletePath(path);
    }
};


/* Run a program. */
static void runProgram(const string & program, Environment env)
{
    /* Create a log file. */
    string logFileName = nixLogDir + "/run.log";
    /* !!! auto-pclose on exit */
    FILE * logFile = popen(("tee -a " + logFileName + " >&2").c_str(), "w"); /* !!! escaping */
    if (!logFile)
        throw SysError(format("creating log file `%1%'") % logFileName);

    /* Create a temporary directory where the build will take
       place. */
    static int counter = 0;
    string tmpDir = (format("/tmp/nix-%1%-%2%") % getpid() % counter++).str();

    if (mkdir(tmpDir.c_str(), 0777) == -1)
        throw SysError(format("creating directory `%1%'") % tmpDir);

    AutoDelete delTmpDir(tmpDir);

    /* Fork a child to build the package. */
    pid_t pid;
    switch (pid = fork()) {
            
    case -1:
        throw SysError("unable to fork");

    case 0: 

        try { /* child */

            if (chdir(tmpDir.c_str()) == -1)
                throw SysError(format("changing into to `%1%'") % tmpDir);

            /* Fill in the environment.  We don't bother freeing
               the strings, since we'll exec or die soon
               anyway. */
            const char * env2[env.size() + 1];
            int i = 0;
            for (Environment::iterator it = env.begin();
                 it != env.end(); it++, i++)
                env2[i] = (new string(it->first + "=" + it->second))->c_str();
            env2[i] = 0;

            /* Dup the log handle into stderr. */
            if (dup2(fileno(logFile), STDERR_FILENO) == -1)
                throw SysError("cannot pipe standard error into log file");
            
            /* Dup stderr to stdin. */
            if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
                throw SysError("cannot dup stderr into stdout");

            /* Make the program executable.  !!! hack. */
            if (chmod(program.c_str(), 0755))
                throw SysError("cannot make program executable");

            /* Execute the program.  This should not return. */
            execle(program.c_str(), baseNameOf(program).c_str(), 0, env2);

            throw SysError(format("unable to execute %1%") % program);
            
        } catch (exception & e) {
            cerr << format("build error: %1%\n") % e.what();
        }
        _exit(1);

    }

    /* parent */

    /* Close the logging pipe.  Note that this should not cause
       the logger to exit until builder exits (because the latter
       has an open file handle to the former). */
    pclose(logFile);
    
    /* Wait for the child to finish. */
    int status;
    if (waitpid(pid, &status, 0) != pid)
        throw Error("unable to wait for child");
    
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw Error("unable to build package");
}


/* Throw an exception if the given platform string is not supported by
   the platform we are executing on. */
static void checkPlatform(const string & platform)
{
    if (platform != thisSystem)
        throw Error(format("a `%1%' is required, but I am a `%2%'")
            % platform % thisSystem);
}


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


FState hash2fstate(Hash hash)
{
    return ATmake("Include(<str>)", ((string) hash).c_str());
}


ATerm termFromId(const FSId & id, string * p)
{
    string path = expandId(id);
    if (p) *p = path;
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % path);
    return t;
}


FSId writeTerm(ATerm t, const string & suffix, string * p)
{
    FSId id = hashTerm(t);

    string path = canonPath(nixStore + "/" + 
        (string) id + suffix + ".nix");
    if (!ATwriteToNamedTextFile(t, path.c_str()))
        throw Error(format("cannot write aterm %1%") % path);

    registerPath(path, id);
    if (p) *p = path;

    return id;
}


void registerSuccessor(const FSId & id1, const FSId & id2)
{
    setDB(nixDB, dbSuccessors, id1, id2);
}


static FSId storeSuccessor(const FSId & id1, FState sc)
{
    FSId id2 = writeTerm(sc, "-s-" + (string) id1, 0);
    registerSuccessor(id1, id2);
    return id2;
}


static void parseIds(ATermList ids, FSIds & out)
{
    while (!ATisEmpty(ids)) {
        char * s;
        ATerm id = ATgetFirst(ids);
        if (!ATmatch(id, "<str>", &s))
            throw badTerm("not an id", id);
        out.push_back(parseHash(s));
        ids = ATgetNext(ids);
    }
}


/* Parse a slice. */
static Slice parseSlice(FState fs)
{
    Slice slice;
    ATermList roots, elems;
    
    if (!ATmatch(fs, "Slice([<list>], [<list>])", &roots, &elems))
        throw badTerm("not a slice", fs);

    parseIds(roots, slice.roots);

    while (!ATisEmpty(elems)) {
        char * s1, * s2;
        ATermList refs;
        ATerm t = ATgetFirst(elems);
        if (!ATmatch(t, "(<str>, <str>, [<list>])", &s1, &s2, &refs))
            throw badTerm("not a slice element", t);
        SliceElem elem;
        elem.path = s1;
        elem.id = parseHash(s2);
        parseIds(refs, elem.refs);
        slice.elems.push_back(elem);
        elems = ATgetNext(elems);
    }

    return slice;
}


static ATermList unparseIds(const FSIds & ids)
{
    ATermList l = ATempty;
    for (FSIds::const_iterator i = ids.begin();
         i != ids.end(); i++)
        l = ATinsert(l,
            ATmake("<str>", ((string) *i).c_str()));
    return ATreverse(l);
}


static FState unparseSlice(const Slice & slice)
{
    ATermList roots = unparseIds(slice.roots);
    
    ATermList elems = ATempty;
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
        elems = ATinsert(elems,
            ATmake("(<str>, <str>, <term>)",
                i->path.c_str(),
                ((string) i->id).c_str(),
                unparseIds(i->refs)));

    return ATmake("Slice(<term>, <term>)", roots, elems);
}


typedef set<FSId> FSIdSet;


Slice normaliseFState(FSId id)
{
    debug(format("normalising fstate"));
    Nest nest(true);

    /* Try to substitute $id$ by any known successors in order to
       speed up the rewrite process. */
    string idSucc;
    while (queryDB(nixDB, dbSuccessors, id, idSucc)) {
        debug(format("successor %1% -> %2%") % (string) id % idSucc);
        id = parseHash(idSucc);
    }

    /* Get the fstate expression. */
    FState fs = termFromId(id);

    /* Already in normal form (i.e., a slice)? */
    if (ATgetType(fs) == AT_APPL && 
        (string) ATgetName(ATgetAFun(fs)) == "Slice")
        return parseSlice(fs);
    
    /* Then we it's a Derive node. */
    ATermList outs, ins, bnds;
    char * builder;
    char * platform;
    if (!ATmatch(fs, "Derive([<list>], [<list>], <str>, <str>, [<list>])",
            &outs, &ins, &builder, &platform, &bnds))
        throw badTerm("not a derive", fs);

    /* Right platform? */
    checkPlatform(platform);
        
    /* Realise inputs (and remember all input paths). */
    FSIds inIds;
    parseIds(ins, inIds);

    typedef map<string, SliceElem> ElemMap;

    ElemMap inMap;

    for (FSIds::iterator i = inIds.begin(); i != inIds.end(); i++) {
        Slice slice = normaliseFState(*i);
        realiseSlice(slice);

        for (SliceElems::iterator j = slice.elems.begin();
             j != slice.elems.end(); j++)
            inMap[j->path] = *j;
    }

    Strings inPaths;
    for (ElemMap::iterator i = inMap.begin(); i != inMap.end(); i++)
        inPaths.push_back(i->second.path);

    /* Build the environment. */
    Environment env;
    while (!ATisEmpty(bnds)) {
        char * s1, * s2;
        ATerm bnd = ATgetFirst(bnds);
        if (!ATmatch(bnd, "(<str>, <str>)", &s1, &s2))
            throw badTerm("tuple of strings expected", bnd);
        env[s1] = s2;
        bnds = ATgetNext(bnds);
    }

    /* Check that none of the output paths exist. */
    typedef pair<string, FSId> OutPath;
    list<OutPath> outPaths;
    while (!ATisEmpty(outs)) {
        ATerm t = ATgetFirst(outs);
        char * s1, * s2;
        if (!ATmatch(t, "(<str>, <str>)", &s1, &s2))
            throw badTerm("string expected", t);
        outPaths.push_back(OutPath(s1, parseHash(s2)));
        inPaths.push_back(s1);
        outs = ATgetNext(outs);
    }

    for (list<OutPath>::iterator i = outPaths.begin(); 
         i != outPaths.end(); i++)
        if (pathExists(i->first))
            throw Error(format("path `%1%' exists") % i->first);

    /* Run the builder. */
    runProgram(builder, env);
        
    Slice slice;

    /* Check whether the output paths were created, and register each
       one. */
    FSIdSet used;
    for (list<OutPath>::iterator i = outPaths.begin(); 
         i != outPaths.end(); i++)
    {
        string path = i->first;
        if (!pathExists(path))
            throw Error(format("path `%1%' does not exist") % path);
        registerPath(path, i->second);
        slice.roots.push_back(i->second);

        Strings refs = filterReferences(path, inPaths);

        SliceElem elem;
        elem.path = path;
        elem.id = i->second;

        for (Strings::iterator j = refs.begin(); j != refs.end(); j++) {
            ElemMap::iterator k;
            if ((k = inMap.find(*j)) != inMap.end()) {
                elem.refs.push_back(k->second.id);
                used.insert(k->second.id);
            } else abort(); /* fix! check in created paths */
        }

        slice.elems.push_back(elem);
    }

    for (ElemMap::iterator i = inMap.begin();
         i != inMap.end(); i++)
    {
        FSIdSet::iterator j = used.find(i->second.id);
        if (j == used.end())
            debug(format("NOT referenced: `%1%'") % i->second.path);
        else {
            debug(format("referenced: `%1%'") % i->second.path);
            slice.elems.push_back(i->second);
        }
    }

    FState nf = unparseSlice(slice);
    debug(printTerm(nf));
    storeSuccessor(id, nf);

    return slice;
}


static void checkSlice(const Slice & slice)
{
    if (slice.elems.size() == 0)
        throw Error("empty slice");

    FSIdSet decl;
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
    {
        debug((string) i->id);
        decl.insert(i->id);
    }
    
    for (FSIds::const_iterator i = slice.roots.begin();
         i != slice.roots.end(); i++)
        if (decl.find(*i) == decl.end())
            throw Error(format("undefined id: %1%") % (string) *i);
    
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
        for (FSIds::const_iterator j = i->refs.begin();
             j != i->refs.end(); j++)
            if (decl.find(*j) == decl.end())
                throw Error(format("undefined id: %1%") % (string) *j);
}


void realiseSlice(const Slice & slice)
{
    debug(format("realising slice"));
    Nest nest(true);

    checkSlice(slice);

    /* Perhaps all paths already contain the right id? */

    bool missing = false;
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
    {
        SliceElem elem = *i;
        string id;
        if (!queryDB(nixDB, dbPath2Id, elem.path, id)) {
            if (pathExists(elem.path))
                throw Error(format("path `%1%' obstructed") % elem.path);
            missing = true;
            break;
        }
        if (parseHash(id) != elem.id)
            throw Error(format("path `%1%' obstructed") % elem.path);
    }

    if (!missing) {
        debug(format("already installed"));
        return;
    }

    /* For each element, expand its id at its path. */
    for (SliceElems::const_iterator i = slice.elems.begin();
         i != slice.elems.end(); i++)
    {
        SliceElem elem = *i;
        expandId(elem.id, elem.path);
    }
}


Strings fstatePaths(FSId id)
{
    Strings paths;

    FState fs = termFromId(id);

    ATermList outs, ins, bnds;
    char * builder;
    char * platform;

    if (ATgetType(fs) == AT_APPL && 
        (string) ATgetName(ATgetAFun(fs)) == "Slice")
    {
        Slice slice = parseSlice(fs);

        /* !!! fix complexity */
        for (FSIds::const_iterator i = slice.roots.begin();
             i != slice.roots.end(); i++)
            for (SliceElems::const_iterator j = slice.elems.begin();
                 j != slice.elems.end(); j++)
                if (*i == j->id) paths.push_back(j->path);
    }

    else if (ATmatch(fs, "Derive([<list>], [<list>], <str>, <str>, [<list>])",
                 &outs, &ins, &builder, &platform, &bnds))
    {
        while (!ATisEmpty(outs)) {
            ATerm t = ATgetFirst(outs);
            char * s1, * s2;
            if (!ATmatch(t, "(<str>, <str>)", &s1, &s2))
                throw badTerm("string expected", t);
            paths.push_back(s1);
            outs = ATgetNext(outs);
        }
    }
    
    else throw badTerm("in fstatePaths", fs);

    return paths;
}
