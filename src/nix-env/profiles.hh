#ifndef __PROFILES_H
#define __PROFILES_H

#include <string>

#include "util.hh"


struct Generation
{
    int number;
    Path path;
    time_t creationTime;
};

typedef list<Generation> Generations;


/* Returns the list of currently present generations for the specified
   profile, sorted by generation number. */
Generations findGenerations(Path profile);
    
Path createGeneration(Path profile, Path outPath, Path drvPath);

void switchLink(Path link, Path target);


#endif /* !__PROFILES_H */
