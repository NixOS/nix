#ifndef __DB_H
#define __DB_H

#include <string>
#include <list>

#include "util.hh"

using namespace std;

typedef pair<string, string> DBPair;
typedef list<DBPair> DBPairs;

void createDB(const string & filename, const string & dbname);

bool queryDB(const string & filename, const string & dbname,
    const string & key, string & data);

bool queryListDB(const string & filename, const string & dbname,
    const string & key, Strings & data);

void setDB(const string & filename, const string & dbname,
    const string & key, const string & data);

void setListDB(const string & filename, const string & dbname,
    const string & key, const Strings & data);

void delDB(const string & filename, const string & dbname,
    const string & key);

void enumDB(const string & filename, const string & dbname,
    DBPairs & contents);

#endif /* !__DB_H */
