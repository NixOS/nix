#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>

#include "hash.hh"
#include "archive.hh"
#include "util.hh"
#include "normalise.hh"
#include "globals.hh"


void realise(Path nePath)
{
    Nest nest(lvlDebug, format("TEST: realising `%1%'") % nePath);
    realiseClosure(normaliseNixExpr(nePath));
}


struct MySink : DumpSink
{
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        /* Don't use cout, it's slow as hell! */
        writeFull(STDOUT_FILENO, data, len);
    }
};


struct MySource : RestoreSource
{
    virtual void operator () (unsigned char * data, unsigned int len)
    {
        readFull(STDIN_FILENO, data, len);
    }
};


void runTests()
{
    verbosity = (Verbosity) 100;

    /* Hashing. */
    string s = "0b0ffd0538622bfe20b92c4aa57254d9";
    Hash h = parseHash(s);
    if ((string) h != s) abort();

    try {
        h = parseHash("blah blah");
        abort();
    } catch (Error err) { };

    try {
        h = parseHash("0b0ffd0538622bfe20b92c4aa57254d99");
        abort();
    } catch (Error err) { };

    /* Path canonicalisation. */
    cout << canonPath("/./../././//") << endl;
    cout << canonPath("/foo/bar") << endl;
    cout << canonPath("///foo/////bar//") << endl;
    cout << canonPath("/././/foo/////bar//.") << endl;
    cout << canonPath("/foo////bar//..///x/") << endl;
    cout << canonPath("/foo////bar//..//..//x/y/../z/") << endl;
    cout << canonPath("/foo/bar/../../../..///") << endl;

    /* Dumping. */

#if 0
    MySink sink;
    dumpPath("scratch", sink);
    cout << (string) hashPath("scratch") << endl;
#endif

    /* Restoring. */
#if 0
    MySource source;
    restorePath("outdir", source);
    cout << (string) hashPath("outdir") << endl;
    return;
#endif

    /* Set up the test environment. */

    mkdir("scratch", 0777);
    mkdir("scratch/db", 0777);

    string testDir = absPath("scratch");
    cout << testDir << endl;

    nixStore = testDir;
    nixLogDir = testDir;
    nixDBPath = testDir + "/db";

    openDB();
    initDB();

    /* Expression evaluation. */

    Path builder1fn;
    builder1fn = addToStore("./test-builder-1.sh");

    ATerm fs1 = ATmake(
        "Closure([<str>], [(<str>, [])])",
        builder1fn.c_str(),
        builder1fn.c_str());
    Path fs1ne = writeTerm(fs1, "-c");

    realise(fs1ne);
    realise(fs1ne);

    string out1h = hashString("foo"); /* !!! bad */
    Path out1fn = nixStore + "/" + (string) out1h + "-hello.txt";
    ATerm fs3 = ATmake(
        "Derive([<str>], [<str>], <str>, <str>, [], [(\"out\", <str>)])",
        out1fn.c_str(),
        fs1ne.c_str(),
        thisSystem.c_str(),
        builder1fn.c_str(),
        out1fn.c_str());
    debug(printTerm(fs3));
    Path fs3ne = writeTerm(fs3, "-d");

    realise(fs3ne);
    realise(fs3ne);


    Path builder4fn = addToStore("./test-builder-2.sh");

    ATerm fs4 = ATmake(
        "Closure([<str>], [(<str>, [])])",
        builder4fn.c_str(),
        builder4fn.c_str());
    Path fs4ne = writeTerm(fs4, "-c");

    realise(fs4ne);

    string out5h = hashString("bar"); /* !!! bad */
    Path out5fn = nixStore + "/" + (string) out5h + "-hello2";
    ATerm fs5 = ATmake(
        "Derive([<str>], [<str>], <str>, <str>, [], [(\"out\", <str>), (\"builder\", <str>)])",
        out5fn.c_str(),
        fs4ne.c_str(),
        thisSystem.c_str(),
        builder4fn.c_str(),
        out5fn.c_str(),
        builder4fn.c_str());
    debug(printTerm(fs5));
    Path fs5ne = writeTerm(fs5, "-d");

    realise(fs5ne);
    realise(fs5ne);
}


void run(Strings args)
{
    runTests();
}


string programId = "test";
