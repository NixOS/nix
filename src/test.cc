#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>

#include "hash.hh"
#include "util.hh"
#include "eval.hh"
#include "values.hh"
#include "globals.hh"


void evalTest(Expr e)
{
    e = evalValue(e);
    cout << (string) hashExpr(e) << ": " << printExpr(e) << endl;
}


struct MySink : DumpSink
{
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        /* Don't use cout, it's slow as hell! */
        write(STDOUT_FILENO, (char *) data, len);
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

    /* Set up the test environment. */

    mkdir("scratch", 0777);

    string testDir = absPath("scratch");
    cout << testDir << endl;

    nixValues = testDir;
    nixLogDir = testDir;
    nixDB = testDir + "/db";

    initDB();

    /* Expression evaluation. */

    evalTest(ATmake("Str(\"Hello World\")"));
    evalTest(ATmake("Bool(True)"));
    evalTest(ATmake("Bool(False)"));
    evalTest(ATmake("App(Lam(\"x\", Var(\"x\")), Str(\"Hello World\"))"));
    evalTest(ATmake("App(App(Lam(\"x\", Lam(\"y\", Var(\"x\"))), Str(\"Hello World\")), Str(\"Hallo Wereld\"))"));
    evalTest(ATmake("App(Lam(\"sys\", Lam(\"x\", [Var(\"x\"), Var(\"sys\")])), Str(\"i686-suse-linux\"))"));

    Hash builder1 = addValue("./test-builder-1.sh");

    Expr e1 = ATmake("Exec(Str(<str>), Hash(<str>), [])",
        thisSystem.c_str(), ((string) builder1).c_str());

    evalTest(e1);

    Hash builder2 = addValue("./test-builder-2.sh");

    Expr e2 = ATmake(
        "Exec(Str(<str>), Hash(<str>), [Tup(Str(\"src\"), <term>)])",
        thisSystem.c_str(), ((string) builder2).c_str(), e1);

    evalTest(e2);

    Hash h3 = addValue("./test-expr-1.nix");
    Expr e3 = ATmake("Deref(Hash(<str>))", ((string) h3).c_str());

    evalTest(e3);
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
