#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

using namespace std;


/* Database names. */

/* dbRefs :: Hash -> Path

   Maintains a mapping from hashes to paths.  This is what we use to
   resolve CHash(hash) content descriptors. */
extern string dbRefs;

/* dbSuccessors :: Hash -> Hash

   Each pair (h1, h2) in this mapping records the fact that a
   successor of an fstate expression with hash h1 is stored in a file
   with hash h2.

   Note that a term $y$ is successor of $x$ iff there exists a
   sequence of rewrite steps that rewrites $x$ into $y$.

   Also note that instead of a successor, $y$ can be any term
   equivalent to $x$, that is, reducing to the same result, as long as
   $x$ is equal to or a successor of $y$.  (This is useful, e.g., for
   shared derivate caching over the network).
*/
extern string dbSuccessors;

/* dbNetSources :: Hash -> URL

   Each pair (hash, url) in this mapping states that the value
   identified by hash can be obtained by fetching the value pointed
   to by url.

   TODO: this should be Hash -> [URL]

   TODO: factor this out into a separate tool? */
extern string dbNetSources;


/* Path names. */

/* nixStore is the directory where we generally store atomic and
   derived files. */
extern string nixStore;

/* nixLogDir is the directory where we log various operations. */ 
extern string nixLogDir;

/* nixDB is the file name of the Berkeley DB database where we
   maintain the dbXXX mappings. */
extern string nixDB;


/* Initialize the databases. */
void initDB();


#endif /* !__GLOBALS_H */
