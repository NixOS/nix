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
    EvalResult r = evalValue(e);

    char * s = ATwriteToString(r.e);
    cout << (string) r.h << ": " << s << endl;
}


struct MySink : DumpSink
{
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        cout.write((char *) data, len);
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

    Hash builder1 = addValue("./test-builder-1.sh");

    evalTest(ATmake("Exec(Str(<str>), External(<str>), [])",
        thisSystem.c_str(), ((string) builder1).c_str()));

    Hash builder2 = addValue("./test-builder-2.sh");

    evalTest(ATmake("Exec(Str(<str>), External(<str>), [])",
        thisSystem.c_str(), ((string) builder2).c_str()));
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
