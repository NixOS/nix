#ifndef __NAMES_H
#define __NAMES_H

#include <string>
#include <list>

#include "util.hh"


struct DrvName
{
    string fullName;
    string name;
    string version;
    unsigned int hits;

    DrvName(const string & s);
    bool matches(DrvName & n);
};


typedef list<DrvName> DrvNames;


int compareVersions(const string & v1, const string & v2);
DrvNames drvNamesFromArgs(const Strings & opArgs);


#endif /* !__NAMES_H */
