#ifndef __SHARED_H
#define __SHARED_H

#include <string>

#include "util.hh"


/* These are not implemented here, but must be implemented by a
   program linking against libmain. */

/* Main program.  Called by main() after the ATerm library has been
   initialised and some default arguments have been processed (and
   removed from `args').  main() will catch all exceptions. */
void run(Strings args);

/* Should print a help message to stdout and return. */
void printHelp();


extern string programId;


#endif /* !__SHARED_H */
