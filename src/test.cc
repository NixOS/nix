#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>

#include "hash.hh"
#include "archive.hh"
#include "util.hh"
#include "fstate.hh"
#include "store.hh"
#include "globals.hh"


void realise(FSId id)
{
    cout << format("realising %1%\n") % (string) id;
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
        if (write(STDOUT_FILENO, (char *) data, len) != (ssize_t) len)
            throw SysError("writing to stdout");
    }
};


struct MySource : RestoreSource
{
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        ssize_t res = read(STDIN_FILENO, (char *) data, len);
        if (res == -1)
            throw SysError("reading from stdin");
        if (res != (ssize_t) len)
            throw Error("not enough data available on stdin");
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

    FState fs1 = ATmake(
        "Slice([<str>], [(<str>, <str>, [])])",
        ((string) builder1id).c_str(),
        builder1fn.c_str(),
        ((string) builder1id).c_str());
    FSId fs1id = writeTerm(fs1, "", 0);

    realise(fs1id);
    realise(fs1id);

    FState fs2 = ATmake(
        "Slice([<str>], [(<str>, <str>, [])])",
        ((string) builder1id).c_str(),
        (builder1fn + "_bla").c_str(),
        ((string) builder1id).c_str());
    FSId fs2id = writeTerm(fs2, "", 0);

    realise(fs2id);
    realise(fs2id);

    string out1fn = nixStore + "/hello.txt";
    string out1id = hashString("foo"); /* !!! bad */
    FState fs3 = ATmake(
        "Derive([(<str>, <str>)], [<str>], <str>, <str>, [(\"out\", <str>)])",
        out1fn.c_str(),
        ((string) out1id).c_str(),
        ((string) fs1id).c_str(),
        ((string) builder1fn).c_str(),
        thisSystem.c_str(),
        out1fn.c_str());
    debug(printTerm(fs3));
    FSId fs3id = writeTerm(fs3, "", 0);

    realise(fs3id);
    realise(fs3id);

#if 0
    FState fs2 = ATmake(
        "Path(<str>, Hash(<str>), [])", 
        (builder1fn + "_bla").c_str(),
        ((string) builder1h).c_str());
    realise(fs2);
    realise(fs2);

    string out1fn = nixStore + "/hello.txt";
    FState fs3 = ATmake(
        "Derive(<str>, <str>, [<term>], <str>, [(\"out\", <str>)])",
        thisSystem.c_str(),
        builder1fn.c_str(),
        fs1,
        out1fn.c_str(),
        out1fn.c_str());
    realise(fs3);
#endif

}


void run(Strings args)
{
    runTests();
}


string programId = "test";
