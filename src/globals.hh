#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

using namespace std;


/* Database names. */

/* dbRefs :: Hash -> FileName

   Maintains a mapping from hashes to filenames within the NixValues
   directory.  This mapping is for performance only; it can be
   reconstructed unambiguously.  The reason is that names in this
   directory are not printed hashes but also might carry some
   descriptive element (e.g., "aterm-2.0-ae749a...").  Without this
   mapping, looking up a value would take O(n) time because we would
   need to read the entire directory. */
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

/* nixValues is the directory where all Nix values (both files and
   directories, and both normal and non-normal forms) live. */
extern string nixValues;

/* nixLogDir is the directory where we log evaluations. */ 
extern string nixLogDir;

/* nixDB is the file name of the Berkeley DB database where we
   maintain the dbXXX mappings. */
extern string nixDB;


/* Initialize the databases. */
void initDB();


#endif /* !__GLOBALS_H */
