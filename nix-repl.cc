#include <iostream>
#include <cstdlib>

#include <readline/readline.h>
#include <readline/history.h>

#include "shared.hh"
#include "eval.hh"

using namespace std;
using namespace nix;


string programId = "nix-repl";


void printHelp()
{
    std::cout << "Usage: nix-repl\n";
}


bool getLine(string & line)
{
    char * s = readline ("nix-repl> ");
    if (!s) return false;
    line = chomp(string(s));
    free(s);
    if (line != "") add_history(line.c_str());
    return true;
}


void run(nix::Strings args)
{
    EvalState state;
    Path curDir = absPath(".");

    while (true) {
        string line;
        if (!getLine(line)) break;

        try {
            Expr * e = state.parseExprFromString(line, curDir);
            Value v;
            state.eval(e, v);
            state.strictForceValue(v);
            std::cout << v << std::endl;
        } catch (Error & e) {
            printMsg(lvlError, e.msg());
        }

        std::cout << std::endl;
    }

    std::cout << std::endl;
}
