#include "globals.hh"
#include "db.hh"


string dbHash2Paths = "hash2paths";
string dbSuccessors = "successors";
string dbSubstitutes = "substitutes";


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDB = "/UNINIT";


void initDB()
{
    createDB(nixDB, dbHash2Paths);
    createDB(nixDB, dbSuccessors);
    createDB(nixDB, dbSubstitutes);
}
