#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

using namespace std;


/* Database names. */

/* dbRefs :: Hash -> Path

   Maintains a mapping from hashes to paths.  This is what we use to
   resolve CHash(hash) content descriptors. */
extern string dbRefs;

/* dbNFs :: Hash -> Hash

   Each pair (h1, h2) in this mapping records the fact that the normal
   form of an expression with hash h1 is Hash(h2).

   TODO: maybe this should be that the normal form of an expression
   with hash h1 is an expression with hash h2; this would be more
   general, but would require us to store lots of small expressions in
   the file system just to support the caching mechanism.
*/
extern string dbNFs;

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
