#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>

#include "hash.hh"
#include "archive.hh"
#include "util.hh"
#include "eval.hh"
#include "values.hh"
#include "globals.hh"


void realise(FState fs)
{
    realiseFState(fs);
}


void realiseFail(FState fs)
{
    try {
        realiseFState(fs);
        abort();
    } catch (Error e) {
        cout << "error (expected): " << e.what() << endl;
    }
}


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

#if 0
    eval(whNormalise,
        ATmake("Str(\"Hello World\")"));
    eval(whNormalise,
        ATmake("Bool(True)"));
    eval(whNormalise,
        ATmake("Bool(False)"));
    eval(whNormalise,
        ATmake("App(Lam(\"x\", Var(\"x\")), Str(\"Hello World\"))"));
    eval(whNormalise,
        ATmake("App(App(Lam(\"x\", Lam(\"y\", Var(\"x\"))), Str(\"Hello World\")), Str(\"Hallo Wereld\"))"));
    eval(whNormalise,
        ATmake("App(Lam(\"sys\", Lam(\"x\", [Var(\"x\"), Var(\"sys\")])), Str(\"i686-suse-linux\"))"));

    evalFail(whNormalise,
        ATmake("Foo(123)"));

    string builder1fn = absPath("./test-builder-1.sh");
    Hash builder1h = hashFile(builder1fn);

    string fn1 = nixValues + "/builder-1.sh";
    Expr e1 = ATmake("File(<str>, ExtFile(<str>, <str>), [])", 
        fn1.c_str(),
        builder1h.c_str(),
        builder1fn.c_str());
    eval(fNormalise, e1);

    string fn2 = nixValues + "/refer.txt";
    Expr e2 = ATmake("File(<str>, Regular(<str>), [<term>])",
        fn2.c_str(),
        ("I refer to " + fn1).c_str(),
        e1);
    eval(fNormalise, e2);

    realise(e2);
#endif

    Hash builder1h;
    string builder1fn;
    addToStore("./test-builder-1.sh", builder1fn, builder1h);

    FState fs1 = ATmake(
        "File(<str>, Hash(<str>), [])", 
        builder1fn.c_str(),
        ((string) builder1h).c_str());
    realiseFState(fs1);
    realiseFState(fs1);

    FState fs2 = ATmake(
        "File(<str>, Hash(<str>), [])", 
        (builder1fn + "_bla").c_str(),
        ((string) builder1h).c_str());
    realiseFState(fs2);
    realiseFState(fs2);

#if 0
    Expr e1 = ATmake("Exec(Str(<str>), Hash(<str>), [])",
        thisSystem.c_str(), ((string) builder1).c_str());

    eval(e1);

    Hash builder2 = addValue("./test-builder-2.sh");

    Expr e2 = ATmake(
        "Exec(Str(<str>), Hash(<str>), [Tup(Str(\"src\"), <term>)])",
        thisSystem.c_str(), ((string) builder2).c_str(), e1);

    eval(e2);

    Hash h3 = addValue("./test-expr-1.nix");
    Expr e3 = ATmake("Deref(Hash(<str>))", ((string) h3).c_str());

    eval(e3);

    deleteValue(h3);
#endif
}


int main(int argc, char * * argv)
{
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    try {
        runTests();
    } catch (exception & e) {
        cerr << "error: " << e.what() << endl;
        return 1;
    }
}
