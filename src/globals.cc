#include "globals.hh"
#include "db.hh"


Database nixDB;


TableId dbPath2Id;
TableId dbId2Paths;
TableId dbSuccessors;
TableId dbSubstitutes;


string nixStore = "/UNINIT";
string nixDataDir = "/UNINIT";
string nixLogDir = "/UNINIT";
string nixDBPath = "/UNINIT";


void openDB()
{
    nixDB.open(nixDBPath);
    dbPath2Id = nixDB.openTable("path2id");
    dbId2Paths = nixDB.openTable("id2paths");
    dbSuccessors = nixDB.openTable("successors");
    dbSubstitutes = nixDB.openTable("substitutes");
}


void initDB()
{
}
