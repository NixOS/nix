#include "globals.hh"
#include "db.hh"


string dbPath2Id = "path2id";
string dbId2Paths = "id2paths";
string dbSuccessors = "successors";
string dbSubstitutes = "substitutes";


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDB = "/UNINIT";


void initDB()
{
    createDB(nixDB, dbPath2Id);
    createDB(nixDB, dbId2Paths);
    createDB(nixDB, dbSuccessors);
    createDB(nixDB, dbSubstitutes);
}
