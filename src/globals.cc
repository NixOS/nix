#include "globals.hh"
#include "db.hh"


string dbRefs = "refs";
string dbSuccessors = "successors";
string dbNetSources = "netsources";

string nixStore = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDB = "/UNINIT";


void initDB()
{
    createDB(nixDB, dbRefs);
    createDB(nixDB, dbSuccessors);
    createDB(nixDB, dbNetSources);
}
