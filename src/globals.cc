#include "globals.hh"
#include "db.hh"


Database nixDB;


TableId dbValidPaths;
TableId dbSuccessors;
TableId dbSuccessorsRev;
TableId dbSubstitutes;
TableId dbSubstitutesRev;


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
    dbSuccessorsRev = nixDB.openTable("successors-rev");
    dbSubstitutes = nixDB.openTable("substitutes");
    dbSubstitutesRev = nixDB.openTable("substitutes-rev");
}


void initDB()
{
}
