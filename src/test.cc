#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>

#include "hash.hh"
#include "archive.hh"
#include "util.hh"
#include "normalise.hh"
#include "globals.hh"


void realise(FSId id)
{
    Nest nest(lvlDebug, format("TEST: realising %1%") % (string) id);
    Slice slice = normaliseFState(id);
    realiseSlice(slice);
}


#if 0
void realiseFail(FState fs)
{
    try {
        realiseFState(fs);
        abort();
    } catch (Error e) {
        cout << "error (expected): " << e.what() << endl;
    }
}
#endif


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
    /* Hashing. */
    string s = "0b0ffd0538622bfe20b92c4aa57254d9";
    Hash h = parseHash(s);
    if ((string) h != s) abort();

    try {
        h = parseHash("blah blah");
        abort();
    } catch (BadRefError err) { };

    try {
        h = parseHash("0b0ffd0538622bfe20b92c4aa57254d99");
        abort();
    } catch (BadRefError err) { };

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

    string testDir = absPath("scratch");
    cout << testDir << endl;

    nixStore = testDir;
    nixLogDir = testDir;
    nixDB = testDir + "/db";

    initDB();

    /* Expression evaluation. */

    FSId builder1id;
    string builder1fn;
    addToStore("./test-builder-1.sh", builder1fn, builder1id);

    ATerm fs1 = ATmake(
        "Slice([<str>], [(<str>, <str>, [])])",
        ((string) builder1id).c_str(),
        builder1fn.c_str(),
        ((string) builder1id).c_str());
    FSId fs1id = writeTerm(fs1, "");

    realise(fs1id);
    realise(fs1id);

    ATerm fs2 = ATmake(
        "Slice([<str>], [(<str>, <str>, [])])",
        ((string) builder1id).c_str(),
        (builder1fn + "_bla").c_str(),
        ((string) builder1id).c_str());
    FSId fs2id = writeTerm(fs2, "");

    realise(fs2id);
    realise(fs2id);

    string out1id = hashString("foo"); /* !!! bad */
    string out1fn = nixStore + "/" + (string) out1id + "-hello.txt";
    ATerm fs3 = ATmake(
        "Derive([(<str>, <str>)], [<str>], <str>, <str>, [(\"out\", <str>)])",
        out1fn.c_str(),
        ((string) out1id).c_str(),
        ((string) fs1id).c_str(),
        ((string) builder1fn).c_str(),
        thisSystem.c_str(),
        out1fn.c_str());
    debug(printTerm(fs3));
    FSId fs3id = writeTerm(fs3, "");

    realise(fs3id);
    realise(fs3id);


    FSId builder4id;
    string builder4fn;
    addToStore("./test-builder-2.sh", builder4fn, builder4id);

    ATerm fs4 = ATmake(
        "Slice([<str>], [(<str>, <str>, [])])",
        ((string) builder4id).c_str(),
        builder4fn.c_str(),
        ((string) builder4id).c_str());
    FSId fs4id = writeTerm(fs4, "");

    realise(fs4id);

    string out5id = hashString("bar"); /* !!! bad */
    string out5fn = nixStore + "/" + (string) out5id + "-hello2";
    ATerm fs5 = ATmake(
        "Derive([(<str>, <str>)], [<str>], <str>, <str>, [(\"out\", <str>), (\"builder\", <str>)])",
        out5fn.c_str(),
        ((string) out5id).c_str(),
        ((string) fs4id).c_str(),
        ((string) builder4fn).c_str(),
        thisSystem.c_str(),
        out5fn.c_str(),
        ((string) builder4fn).c_str());
    debug(printTerm(fs5));
    FSId fs5id = writeTerm(fs5, "");

    realise(fs5id);
    realise(fs5id);
}


void run(Strings args)
{
    runTests();
}


string programId = "test";
