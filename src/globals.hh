#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

using namespace std;


/* Database names. */

/* dbHash2Paths :: Hash -> [Path]

   Maintains a mapping from hashes to lists of paths.  This is what we
   use to resolve Hash(hash) content descriptors. */
extern string dbHash2Paths;

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

/* dbSubstitutes :: Hash -> [Hash]

   Each pair $(h, [hs])$ tells Nix that it can realise any of the
   fstate expressions referenced by the hashes in $hs$ to obtain a Nix
   archive that, when unpacked, will produce a path with hash $h$.

   The main purpose of this is for distributed caching of derivates.
   One system can compute a derivate with hash $h$ and put it on a
   website (as a Nix archive), for instance, and then another system
   can register a substitute for that derivate.  The substitute in
   this case might be an fstate expression that fetches the Nix
   archive. 
*/
extern string dbSubstitutes;


/* Path names. */

/* nixStore is the directory where we generally store atomic and
   derived files. */
extern string nixStore;

extern string nixDataDir; /* !!! fix */

/* nixLogDir is the directory where we log various operations. */ 
extern string nixLogDir;

/* nixDB is the file name of the Berkeley DB database where we
   maintain the dbXXX mappings. */
extern string nixDB;


/* Initialize the databases. */
void initDB();


#endif /* !__GLOBALS_H */
