#include "aterm.hh"
#include <iostream>


void runTests()
{
    verbosity = lvlDebug;

    ATMatcher pos;

    ATerm t = ATmake("Call(Foo, Bar, \"xyz\")");
    
    debug(format("term: %1%") % t);

    string fun, arg3;
    ATerm lhs, rhs;

    if (!(atMatch(pos, t) >> "Call" >> lhs >> rhs >> arg3))
        throw Error("should succeed");
    if (arg3 != "xyz") throw Error("bad 1");

    if (!(atMatch(pos, t) >> fun >> lhs >> rhs >> arg3))
        throw Error("should succeed");
    if (fun != "Call") throw Error("bad 2");
    if (arg3 != "xyz") throw Error("bad 3");

    if (!(atMatch(pos, t) >> fun >> lhs >> rhs >> "xyz"))
        throw Error("should succeed");

    if (atMatch(pos, t) >> fun >> lhs >> rhs >> "abc")
        throw Error("should fail");

    if (atMatch(pos, t) >> "Call" >> lhs >> rhs >> "abc")
        throw Error("should fail");

    t = ATmake("X([A, B, C], \"abc\")");

    ATerm t1, t2, t3;
    if (atMatch(pos, t) >> "X" >> t1 >> t2 >> t3)
        throw Error("should fail");
    if (!(atMatch(pos, t) >> "X" >> t1 >> t2))
        throw Error("should succeed");
    ATermList ts;
    if (!(atMatch(pos, t) >> "X" >> ts >> t2))
        throw Error("should succeed");
    if (ATgetLength(ts) != 3)
        throw Error("bad");
    if (atMatch(pos, t) >> "X" >> t1 >> ts)
        throw Error("should fail");
}


int main(int argc, char * * argv)
{
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    try {
        runTests();
    } catch (Error & e) {
        printMsg(lvlError, format("error: %1%") % e.msg());
        return 1;
    }

    return 0;
}
