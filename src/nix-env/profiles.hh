#ifndef __PROFILES_H
#define __PROFILES_H

#include <string>

#include "util.hh"


Path createGeneration(Path profile, Path outPath, Path drvPath);

void switchLink(Path link, Path target);


#endif /* !__PROFILES_H */
