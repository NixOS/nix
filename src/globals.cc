#include "globals.hh"
#include "db.hh"


string dbRefs = "refs";
string dbNFs = "nfs";
string dbNetSources = "netsources";

string nixValues = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDB = "/UNINIT";


void initDB()
{
    createDB(nixDB, dbRefs);
    createDB(nixDB, dbNFs);
    createDB(nixDB, dbNetSources);
}
