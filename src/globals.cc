#include "globals.hh"
#include "db.hh"


Database nixDB;


TableId dbValidPaths;
TableId dbSuccessors;
TableId dbSubstitutes;


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDBPath = "/UNINIT";


bool keepFailed = false;


void openDB()
{
    nixDB.open(nixDBPath);
    dbValidPaths = nixDB.openTable("validpaths");
    dbSuccessors = nixDB.openTable("successors");
    dbSubstitutes = nixDB.openTable("substitutes");
}


void initDB()
{
}
