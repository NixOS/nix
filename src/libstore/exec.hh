#ifndef __EXEC_H
#define __EXEC_H

#include <string>
#include <map>

#include "util.hh"

using namespace std;


/* A Unix environment is a mapping from strings to strings. */
typedef map<string, string> Environment;


/* Run a program. */
void runProgram(const string & program, 
    const Strings & args, Environment env,
    const string & logFileName);


#endif /* !__EXEC_H */
