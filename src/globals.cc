#include "globals.hh"
#include "db.hh"


Database nixDB;


string dbPath2Id = "path2id";
string dbId2Paths = "id2paths";
string dbSuccessors = "successors";
string dbSubstitutes = "substitutes";


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDBPath = "/UNINIT";


void openDB()
{
    nixDB.open(nixDBPath);
}


void initDB()
{
    nixDB.createTable(dbPath2Id);
    nixDB.createTable(dbId2Paths);
    nixDB.createTable(dbSuccessors);
    nixDB.createTable(dbSubstitutes);
}
